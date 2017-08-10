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
import com.google.common.collect.Lists;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;

import java.util.List;

public class SetUserPropertyStmtTest {
    private Analyzer analyzer;

    @Before
    public void setUp() {
        analyzer = AccessTestUtil.fetchAdminAnalyzer(true);
    }

    @Test
    public void testNormal() throws InternalException, AnalysisException {
        List<SetVar> propertyVarList = Lists.newArrayList();
        propertyVarList.add(new SetUserPropertyVar("load_cluster.palo-dpp", null));
        propertyVarList.add(new SetUserPropertyVar("quota.normal", "100"));

        SetUserPropertyStmt stmt = new SetUserPropertyStmt("testUser", propertyVarList);
        stmt.analyze(analyzer);
        Assert.assertEquals("testCluster:testUser", stmt.getUser());
        Assert.assertEquals(
                "SET PROPERTY FOR 'testCluster:testUser' 'load_cluster.palo-dpp' = NULL, 'quota.normal' = '100'",
                stmt.toString());
    }

    @Test(expected = AnalysisException.class)
    public void testNoProperty() throws InternalException, AnalysisException {
        SetUserPropertyStmt stmt = new SetUserPropertyStmt("testUser", null);
        stmt.analyze(analyzer);
        Assert.fail("No exception throws");
    }
}