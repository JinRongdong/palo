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

#ifndef BDG_PALO_BE_SRC_OLAP_COLUMN_FILE_RUN_LENGTH_BYTE_WRITER_H
#define BDG_PALO_BE_SRC_OLAP_COLUMN_FILE_RUN_LENGTH_BYTE_WRITER_H

#include "olap/column_file/stream_index_writer.h"
#include "olap/olap_define.h"

namespace palo {
namespace column_file {

class OutStream;
class RowIndexEntryMessage;

// A Writer that writes a sequence of bytes. A control byte is written before
// each run with positive values 0 to 127 meaning 2 to 129 repetitions. If the
// bytes is -1 to -128, 1 to 128 literal byte values follow.
class RunLengthByteWriter {
public:
    explicit RunLengthByteWriter(OutStream* output);
    ~RunLengthByteWriter() {}
    OLAPStatus write(char byte);
    OLAPStatus flush();
    void get_position(PositionEntryWriter* index_entry) const;
    static const int32_t MIN_REPEAT_SIZE = 3;
    static const int32_t MAX_LITERAL_SIZE = 128;
    static const int32_t MAX_REPEAT_SIZE = 127 + MIN_REPEAT_SIZE;
private:
    OLAPStatus _write_values();

    OutStream* _output;
    char _literals[MAX_LITERAL_SIZE];
    int32_t _num_literals;
    bool _repeat;
    int32_t _tail_run_length;

    DISALLOW_COPY_AND_ASSIGN(RunLengthByteWriter);
};

}  // namespace column_file
}  // namespace palo

#endif // BDG_PALO_BE_SRC_OLAP_COLUMN_FILE_RUN_LENGTH_BYTE_WRITER_H
