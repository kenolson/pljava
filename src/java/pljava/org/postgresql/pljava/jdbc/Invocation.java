/*
 * Copyright (c) 2003, 2004 TADA AB - Taby Sweden
 * Distributed under the terms shown in the file COPYRIGHT.
 */
package org.postgresql.pljava.jdbc;

import java.sql.SQLException;

import org.postgresql.pljava.internal.Backend;

/**
 * @author Thomas Hallgren
 */
public class Invocation
{
	/**
	 * The current "stack" of invocations.
	 */
	private static Invocation[] s_levels = new Invocation[10];

	/**
	 * Nesting level for this invocation
	 */
	private final int m_nestingLevel;

	/**
	 * Top level savepoint relative to this invocation.
	 */
	private SPISavepoint m_savepoint;

	private Invocation(int level)
	{
		m_nestingLevel = level;
	}

	/**
	 * @return The nesting level of this invocation
	 */
	public int getNestingLevel()
	{
		return m_nestingLevel;
	}

	/**
	 * @return Returns the savePoint.
	 */
	final SPISavepoint getSavepoint()
	{
		return m_savepoint;
	}

	/**
	 * @param savepoint The savepoint to set.
	 */
	final void setSavepoint(SPISavepoint savepoint)
	{
		m_savepoint = savepoint;
	}

	/**
	 * Called from the backend when the invokation exits. Should
	 * not be invoked any other way.
	 */
	public void onExit()
	throws SQLException
	{
		try
		{
			if(m_savepoint != null)
				m_savepoint.onInvocationExit();
		}
		finally
		{
			s_levels[m_nestingLevel] = null;
		}
	}

	/**
	 * @return The current invocation
	 */
	public static Invocation current()
	{
		synchronized(Backend.THREADLOCK)
		{
			Invocation curr;
			int level = _getNestingLevel();
			int top = s_levels.length;
			if(top > level)
			{
				curr = s_levels[level];
				if(curr != null)
					return curr;
			}
			else
			{
				int newSize = top;
				do { newSize <<= 2; } while(newSize <= level);
				Invocation[] levels = new Invocation[newSize];
				System.arraycopy(s_levels, 0, levels, 0, top);
				s_levels = levels;
			}
			curr = new Invocation(level);
			s_levels[level] = curr;
			curr._register();
			return curr;
		}
	}

	static void clearErrorCondition()
	{
		synchronized(Backend.THREADLOCK)
		{
			_clearErrorCondition();
		}
	}

	/**
	 * Register this Invocation so that it receives the onExit callback
	 */
	private native void  _register();
	
	/**
	 * Returns the current nesting level
	 */
	private native static int  _getNestingLevel();
	
	/**
	 * Clears the error condition set by elog(ERROR)
	 */
	private native static void  _clearErrorCondition();
}