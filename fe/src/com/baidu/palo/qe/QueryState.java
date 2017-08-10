// Copyright (c) 2017, Baidu.com, Inc. All Rights Reserved

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

package com.baidu.palo.qe;

import com.baidu.palo.common.ErrorCode;
import com.baidu.palo.mysql.MysqlEofPacket;
import com.baidu.palo.mysql.MysqlErrPacket;
import com.baidu.palo.mysql.MysqlOkPacket;
import com.baidu.palo.mysql.MysqlPacket;

// query state used to record state of query, maybe query status is better
public class QueryState {
    public enum MysqlStateType {
        NOOP,   // send nothing to remote
        OK,     // send OK packet to remote
        EOF,    // send EOF packet to remote
        ERR     // send ERROR packet to remote
    }

    public enum ErrType {
        ANALYSIS_ERR,
        OTHER_ERR
    }

    private MysqlStateType stateType = MysqlStateType.OK;
    private String errorMessage = "";
    private ErrorCode errorCode;
    private String infoMessage;
    private ErrType errType = ErrType.OTHER_ERR;

    public QueryState() {
    }

    public void reset() {
        stateType = MysqlStateType.OK;
        infoMessage = null;
    }

    public MysqlStateType getStateType() {
        return stateType;
    }

    public void setEof() {
        stateType = MysqlStateType.EOF;
    }

    public void setOk() {
        stateType = MysqlStateType.OK;
    }

    public void setOk(String infoMessage) {
        stateType = MysqlStateType.OK;
        this.infoMessage = infoMessage;
    }

    public void setError(String errorMsg) {
        this.stateType = MysqlStateType.ERR;
        this.errorMessage = errorMsg;
    }

    public void setError(ErrorCode code, String msg) {
        this.stateType = MysqlStateType.ERR;
        this.errorCode = code;
        this.errorMessage = msg;
    }

    public void setErrType(ErrType errType) {
        this.errType = errType;
    }

    public ErrType getErrType() {
        return errType;
    }

    public String getInfoMessage() {
        return infoMessage;
    }

    public String getErrorMessage() {
        return errorMessage;
    }

    public ErrorCode getErrorCode() {
        return errorCode;
    }

    public MysqlPacket toResponsePacket() {
        MysqlPacket packet = null;
        switch (stateType) {
            case OK:
                packet = new MysqlOkPacket(this);
                break;
            case EOF:
                packet = new MysqlEofPacket(this);
                break;
            case ERR:
                packet = new MysqlErrPacket(this);
                break;
            default:
                break;
        }
        return packet;
    }

    @Override
    public String toString() {
        return String.valueOf(stateType);
    }
}
