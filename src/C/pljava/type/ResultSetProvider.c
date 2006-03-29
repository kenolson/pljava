/*
 * Copyright (c) 2004, 2005, 2006 TADA AB - Taby Sweden
 * Distributed under the terms shown in the file COPYRIGHT
 * found in the root folder of this project or at
 * http://eng.tada.se/osprojects/COPYRIGHT.html
 *
 * @author Thomas Hallgren
 */
#include <postgres.h>
#include <utils/memutils.h>
#include <utils/numeric.h>
#include <utils/typcache.h>
#include <nodes/execnodes.h>
#include <funcapi.h>
#include <executor/spi.h>

#include "pljava/backports.h"
#include "pljava/Invocation.h"
#include "pljava/type/Type_priv.h"
#include "pljava/type/ComplexType.h"
#include "pljava/type/TupleDesc.h"
#include "pljava/type/SingleRowWriter.h"
#include "pljava/type/ResultSetProvider.h"
#include "pljava/Backend.h"
#include "pljava/HashMap.h"
#include "pljava/Exception.h"

static jclass s_ResultSetProvider_class;
static jmethodID s_ResultSetProvider_assignRowValues;
static jmethodID s_ResultSetProvider_close;

static jclass s_ResultSetHandle_class;
static jclass s_ResultSetPicker_class;
static jmethodID s_ResultSetPicker_init;

static TypeClass s_ResultSetProviderClass;
static TypeClass s_ResultSetHandleClass;
static Type s_ResultSetHandle;
static HashMap s_idCache;
static HashMap s_modCache;

/* Structure used in multi function calls (calls returning
 * SETOF <complex type>)
 */
typedef struct
{
	jobject       singleRowWriter;
	jobject       resultSetProvider;
	jobject       invocation;
	MemoryContext rowContext;
	MemoryContext spiContext;
	bool          hasConnected;
	bool          trusted;
} CallContextData;

static void _ResultSetProvider_closeIteration(CallContextData* ctxData)
{
	currentInvocation->hasConnected = ctxData->hasConnected;
	currentInvocation->invocation   = ctxData->invocation;

	JNI_callVoidMethod(ctxData->resultSetProvider, s_ResultSetProvider_close);
	JNI_deleteGlobalRef(ctxData->singleRowWriter);
	JNI_deleteGlobalRef(ctxData->resultSetProvider);

	if(ctxData->hasConnected && ctxData->spiContext != 0)
	{
		/* Connect during SRF_IS_FIRSTCALL(). Switch context back to what
		 * it was at that time and disconnect.
		 */
		MemoryContext currCtx = MemoryContextSwitchTo(ctxData->spiContext);
		Invocation_assertDisconnect();
		MemoryContextSwitchTo(currCtx);
	}
}

static void _ResultSetProvider_endOfSetCB(Datum arg)
{
	Invocation topCall;
	bool saveInExprCtxCB;
	CallContextData* ctxData = (CallContextData*)DatumGetPointer(arg);
	if(currentInvocation == 0)
		Invocation_pushInvocation(&topCall, ctxData->trusted);

	saveInExprCtxCB = currentInvocation->inExprContextCB;
	currentInvocation->inExprContextCB = true;
	_ResultSetProvider_closeIteration(ctxData);
	currentInvocation->inExprContextCB = saveInExprCtxCB;
}

static Datum _ResultSetProvider_invoke(Type self, jclass cls, jmethodID method, jvalue* args, PG_FUNCTION_ARGS)
{
	bool hasRow;
	CallContextData* ctxData;
	FuncCallContext* context;
	MemoryContext currCtx;

	/* stuff done only on the first call of the function
	 */
	if(SRF_IS_FIRSTCALL())
	{
		jobject tmp;
		jobject tmp2;
		ReturnSetInfo* rsInfo;
		TupleDesc tupleDesc;

		/* create a function context for cross-call persistence
		 */
		context = SRF_FIRSTCALL_INIT();
		currCtx = MemoryContextSwitchTo(context->multi_call_memory_ctx);

		/* Call the declared Java function. It returns a ResultSetProvider
		 * or a ResultSet. A ResultSet must be wrapped in a ResultSetPicker
		 * (implements ResultSetProvider).
		 */
		tmp = JNI_callStaticObjectMethodA(cls, method, args);
		if(tmp == 0)
		{
			Invocation_assertDisconnect();
			MemoryContextSwitchTo(currCtx);
			fcinfo->isnull = true;
			SRF_RETURN_DONE(context);
		}

		if(JNI_isInstanceOf(tmp, s_ResultSetHandle_class))
		{
			jobject wrapper = JNI_newObject(s_ResultSetPicker_class, s_ResultSetPicker_init, tmp);
			JNI_deleteLocalRef(tmp);
			tmp = wrapper;
		}

		/* Build a tuple description for the tuples (will be cached
		 * in TopMemoryContext)
		 */
		rsInfo = (ReturnSetInfo*)fcinfo->resultinfo;

		tupleDesc = Type_getTupleDesc(self, fcinfo);
		if(tupleDesc == 0)
			ereport(ERROR, (errmsg("Unable to find tuple descriptor")));

		/* Create the context used by Pl/Java
		 */
		ctxData = (CallContextData*)palloc(sizeof(CallContextData));
		context->user_fctx = ctxData;

		ctxData->resultSetProvider = JNI_newGlobalRef(tmp);
		JNI_deleteLocalRef(tmp);
		tmp = TupleDesc_create(tupleDesc);
		tmp2 = SingleRowWriter_create(tmp);
		JNI_deleteLocalRef(tmp);		
		ctxData->singleRowWriter = JNI_newGlobalRef(tmp2);
		JNI_deleteLocalRef(tmp2);

		ctxData->trusted       = currentInvocation->trusted;
		ctxData->hasConnected  = currentInvocation->hasConnected;
		ctxData->invocation    = currentInvocation->invocation;
		if(ctxData->hasConnected)
			ctxData->spiContext = CurrentMemoryContext;
		else
			ctxData->spiContext = 0;

		ctxData->rowContext = AllocSetContextCreate(context->multi_call_memory_ctx,
								  "PL/Java row context",
								  ALLOCSET_DEFAULT_MINSIZE,
								  ALLOCSET_DEFAULT_INITSIZE,
								  ALLOCSET_DEFAULT_MAXSIZE);

		/* Register callback to be called when the function ends
		 */
		RegisterExprContextCallback(rsInfo->econtext, _ResultSetProvider_endOfSetCB, PointerGetDatum(ctxData));
		MemoryContextSwitchTo(currCtx);
	}

	context = SRF_PERCALL_SETUP();
	ctxData = (CallContextData*)context->user_fctx;
	currCtx = MemoryContextSwitchTo(ctxData->rowContext);
	currentInvocation->hasConnected = ctxData->hasConnected;
	currentInvocation->invocation   = ctxData->invocation;


	/* Obtain next row using the RowProvider as a parameter to the
	 * ResultSetProvider.assignRowValues method.
	 */
	hasRow = (JNI_callBooleanMethod(ctxData->resultSetProvider,
			s_ResultSetProvider_assignRowValues,
			ctxData->singleRowWriter,
			(jint)context->call_cntr) == JNI_TRUE);

	ctxData->hasConnected = currentInvocation->hasConnected;
	ctxData->invocation   = currentInvocation->invocation;
	currentInvocation->hasConnected = false;
	currentInvocation->invocation   = 0;

	if(hasRow)
	{
		Datum result = 0;
		HeapTuple tuple = SingleRowWriter_getTupleAndClear(ctxData->singleRowWriter);
		if(tuple != 0)
			result = HeapTupleGetDatum(tuple);

		MemoryContextSwitchTo(currCtx);
		MemoryContextReset(ctxData->rowContext);
		SRF_RETURN_NEXT(context, result);
	}

	MemoryContextSwitchTo(currCtx);

	/* Unregister this callback and call it manually. We do this because
	 * otherwise it will be called when the backend is in progress of
	 * cleaning up Portals. If we close cursors (i.e. drop portals) in
	 * the close, then that mechanism fails since attempts are made to
	 * delete portals more then once.
	 */
	UnregisterExprContextCallback(
		((ReturnSetInfo*)fcinfo->resultinfo)->econtext,
		_ResultSetProvider_endOfSetCB,
		PointerGetDatum(ctxData));
	
	_ResultSetProvider_closeIteration(ctxData);

	/* This is the end of the set.
	 */
	SRF_RETURN_DONE(context);
}

static jvalue _ResultSetProvider_coerceDatum(Type self, Datum nothing)
{
	jvalue result;
	result.j = 0L;
	return result;
}

static Datum _ResultSetProvider_coerceObject(Type self, jobject nothing)
{
	return 0;
}

static Type ResultSetHandle_obtain(Oid typeId)
{
	return s_ResultSetHandle;
}

static Type ResultSetProvider_obtain(Oid typeId)
{
	return (Type)ComplexType_createType(
		s_ResultSetProviderClass, s_idCache, s_modCache, lookup_rowtype_tupdesc(typeId, -1));
}

Type ResultSetProvider_createType(Oid typid, TupleDesc tupleDesc)
{
	return (Type)ComplexType_createType(
		s_ResultSetProviderClass, s_idCache, s_modCache, tupleDesc);
}

extern void ResultSetProvider_initialize(void);
void ResultSetProvider_initialize(void)
{
	s_ResultSetProvider_class = JNI_newGlobalRef(PgObject_getJavaClass("org/postgresql/pljava/ResultSetProvider"));

	s_ResultSetProvider_assignRowValues = PgObject_getJavaMethod(s_ResultSetProvider_class, "assignRowValues", "(Ljava/sql/ResultSet;I)Z");
	s_ResultSetProvider_close = PgObject_getJavaMethod(s_ResultSetProvider_class, "close", "()V");
	s_ResultSetHandle_class = JNI_newGlobalRef(PgObject_getJavaClass("org/postgresql/pljava/ResultSetHandle"));
	s_ResultSetPicker_class = JNI_newGlobalRef(PgObject_getJavaClass("org/postgresql/pljava/internal/ResultSetPicker"));
	s_ResultSetPicker_init = PgObject_getJavaMethod(s_ResultSetPicker_class, "<init>", "(Lorg/postgresql/pljava/ResultSetHandle;)V");

	s_idCache = HashMap_create(13, TopMemoryContext);
	s_modCache = HashMap_create(13, TopMemoryContext);

	s_ResultSetProviderClass = ComplexTypeClass_alloc("type.ResultSetProvider");
	s_ResultSetProviderClass->JNISignature = "Lorg/postgresql/pljava/ResultSetProvider;";
	s_ResultSetProviderClass->javaTypeName = "org.postgresql.pljava.ResultSetProvider";
	s_ResultSetProviderClass->invoke	   = _ResultSetProvider_invoke;
	s_ResultSetProviderClass->coerceDatum  = _ResultSetProvider_coerceDatum;
	s_ResultSetProviderClass->coerceObject = _ResultSetProvider_coerceObject;
	Type_registerType(InvalidOid, "org.postgresql.pljava.ResultSetProvider", ResultSetProvider_obtain);

	s_ResultSetHandleClass = TypeClass_alloc("type.ResultSetHandle");
	s_ResultSetHandleClass->JNISignature = "Lorg/postgresql/pljava/ResultSetHandle;";
	s_ResultSetHandleClass->javaTypeName = "org.postgresql.pljava.ResultSetHandle";
	s_ResultSetHandle = TypeClass_allocInstance(s_ResultSetHandleClass, InvalidOid);

	Type_registerType(InvalidOid, "org.postgresql.pljava.ResultSetHandle", ResultSetHandle_obtain);
}
