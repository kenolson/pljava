/*
 * Copyright (c) 2015- Tada AB and other contributors, as listed below.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the The BSD 3-Clause License
 * which accompanies this distribution, and is available at
 * http://opensource.org/licenses/BSD-3-Clause
 *
 * Contributors:
 *   Chapman Flack
 */
package org.postgresql.pljava.example.annotation;

import java.sql.SQLData;
import java.sql.SQLException;
import java.sql.SQLInput;
import java.sql.SQLOutput;

import org.postgresql.pljava.annotation.SQLAction;
import org.postgresql.pljava.annotation.BaseUDT;

/**
 * A User Defined Type with varlena storage, tesing github issue 52.
 *
 * This looks from SQL like an integer type, but is stored in <em>unary</em>:
 * the integer value <em>n</em> is represented by <em>n</em> {@code a}
 * characters. That makes it easy to test how big a value gets correctly stored
 * and retrieved. It should be about a GB, but in issue 52 was failing at 32768
 * because of a narrowing assignment in the native code.
 */
@SQLAction(requires="varlena UDT", install=
"  SELECT CASE v::text = v::javatest.VarlenaUDTTest::text " +
"   WHEN true THEN javatest.logmessage('INFO', 'works for ' || v) " +
"   ELSE javatest.logmessage('WARNING', 'fails for ' || v) " +
"   END " +
"   FROM (VALUES (('32767')), (('32768')), (('65536')), (('1048576'))) " +
"   AS t ( v )"
)
@BaseUDT(schema="javatest", provides="varlena UDT")
public class VarlenaUDTTest implements SQLData {
	int apop;
	String typname;

	public VarlenaUDTTest() { }

	public static VarlenaUDTTest parse( String s, String typname) {
		int i = Integer.parseInt( s);
		VarlenaUDTTest u = new VarlenaUDTTest();
		u.apop = i;
		u.typname = typname;
		return u;
	}

	public String toString() {
		return String.valueOf( apop);
	}

	public String getSQLTypeName() {
		return typname;
	}

	public void writeSQL( SQLOutput stream) throws SQLException {
		for ( int i = 0 ; i < apop ; ++ i )
			stream.writeByte( (byte)'a');
	}

	public void readSQL( SQLInput stream, String typname) throws SQLException {
		this.typname = typname;
		int i = 0;
		for ( ;; ++i )
			try {
				stream.readByte();
			}
			catch ( SQLException sqle ) {
				if ( "Unexpected EOF on data input".equals( sqle.getMessage()) )
					break;
				throw sqle;
			}
		apop = i;
	}
}
