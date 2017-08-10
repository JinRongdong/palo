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
import com.baidu.palo.common.FeNameFormat;

import com.google.common.base.Strings;

import java.util.Map;

// rename table
public class RollupRenameClause extends AlterClause {
    private String rollupName;
    private String newRollupName;

    public RollupRenameClause(String rollupName, String newRollupName) {
        this.rollupName = rollupName;
        this.newRollupName = newRollupName;
    }

    public String getRollupName() {
        return rollupName;
    }

    public String getNewRollupName() {
        return newRollupName;
    }

    @Override
    public void analyze(Analyzer analyzer) throws AnalysisException {
        if (Strings.isNullOrEmpty(rollupName)) {
            throw new AnalysisException("Rollup name is not set");
        }

        if (Strings.isNullOrEmpty(newRollupName)) {
            throw new AnalysisException("New rollup name is not set");
        }

        FeNameFormat.checkTableName(newRollupName);
    }

    @Override
    public Map<String, String> getProperties() {
        return null;
    }

    @Override
    public String toSql() {
        return "RENAME ROLLUP " + rollupName + " " + newRollupName;
    }

    @Override
    public String toString() {
        return toSql();
    }
}
