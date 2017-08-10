// Modifications copyright (C) 2017, Baidu.com, Inc.
// Copyright 2017 The Apache Software Foundation

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package com.baidu.palo.analysis;

import com.baidu.palo.common.AnalysisException;
import com.baidu.palo.common.InternalException;

import org.junit.Assert;
import org.junit.Test;

public class ShowCreateDbStmtTest {
    @Test
    public void testNormal() throws AnalysisException, InternalException {
        ShowCreateDbStmt stmt = new ShowCreateDbStmt("testDb");
        stmt.analyze(AccessTestUtil.fetchAdminAnalyzer(true));
        Assert.assertEquals("testCluster:testDb", stmt.getDb());
        Assert.assertEquals(2, stmt.getMetaData().getColumnCount());
        Assert.assertEquals("SHOW CREATE DATABASE `testCluster:testDb`", stmt.toString());
    }

    @Test(expected = AnalysisException.class)
    public void testEmptyDb() throws AnalysisException, InternalException {
        ShowCreateDbStmt stmt = new ShowCreateDbStmt("");
        stmt.analyze(AccessTestUtil.fetchAdminAnalyzer(false));
        Assert.fail("No exception throws.");
    }

    @Test(expected = AnalysisException.class)
    public void testNoPriv() throws AnalysisException, InternalException {
        ShowCreateDbStmt stmt = new ShowCreateDbStmt("testDb");
        stmt.analyze(AccessTestUtil.fetchBlockAnalyzer());
        Assert.fail("No exception throws.");
    }
}
