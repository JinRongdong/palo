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

#include "runtime/export_sink.h"
#include <sstream>

#include "exprs/expr.h"
#include "runtime/runtime_state.h"
#include "runtime/mysql_table_sink.h"
#include "runtime/mem_tracker.h"
#include "runtime/tuple_row.h"
#include "util/runtime_profile.h"
#include "util/debug_util.h"
#include "exec/local_file_writer.h"
#include "exec/broker_writer.h"
#include <thrift/protocol/TDebugProtocol.h>

namespace palo {

ExportSink::ExportSink(ObjectPool* pool,
                       const RowDescriptor& row_desc,
                       const std::vector<TExpr>& t_exprs) :
        _pool(pool),
        _row_desc(row_desc),
        _t_output_expr(t_exprs),
        _bytes_written_counter(nullptr),
        _rows_written_counter(nullptr),
        _write_timer(nullptr) {
}

ExportSink::~ExportSink() {
}

Status ExportSink::init(const TDataSink& t_sink) {
    RETURN_IF_ERROR(DataSink::init(t_sink));
    _t_export_sink = t_sink.export_sink;

    // From the thrift expressions create the real exprs.
    RETURN_IF_ERROR(Expr::create_expr_trees(_pool, _t_output_expr, &_output_expr_ctxs));
    return Status::OK;
}

Status ExportSink::prepare(RuntimeState* state) {
    RETURN_IF_ERROR(DataSink::prepare(state));

    _state = state;

    std::stringstream title;
    title << "ExportSink (frag_id=" << state->fragment_instance_id() << ")";
    // create profile
    _profile = state->obj_pool()->add(new RuntimeProfile(state->obj_pool(), title.str()));
    SCOPED_TIMER(_profile->total_time_counter());

    _mem_tracker.reset(new MemTracker(-1, "ExportSink", state->instance_mem_tracker()));

    // Prepare the exprs to run.
    RETURN_IF_ERROR(Expr::prepare(_output_expr_ctxs, state, _row_desc, _mem_tracker.get()));

    // TODO(lingbin): add some Counter
    _bytes_written_counter = ADD_COUNTER(profile(), "BytesExported", TUnit::BYTES);
    _rows_written_counter = ADD_COUNTER(profile(), "RowsExported", TUnit::UNIT);
    _write_timer = ADD_TIMER(profile(), "WriteTime");

    return Status::OK;
}

Status ExportSink::open(RuntimeState* state) {
    // Prepare the exprs to run.
    RETURN_IF_ERROR(Expr::open(_output_expr_ctxs, state));
    // open broker
    RETURN_IF_ERROR(open_file_writer());
    return Status::OK;
}

Status ExportSink::send(RuntimeState* state, RowBatch* batch) {
    LOG(ERROR) << "debug: export_sink send batch: " << print_batch(batch);
    SCOPED_TIMER(_profile->total_time_counter());
    int num_rows = batch->num_rows();
    std::stringstream ss;
    for (int i = 0; i < num_rows; ++i) {
        ss.str("");
        RETURN_IF_ERROR(gen_row_buffer(batch->get_row(i), &ss));
        LOG(ERROR) << "debug: export_sink send row: " << ss.str();
        const std::string& buf = ss.str();
        size_t written_len = 0;

        SCOPED_TIMER(_write_timer);
        // TODO(lingbin): for broker writer, we should not send rpc each row.
        _file_writer->write(reinterpret_cast<const uint8_t*>(buf.c_str()),
                             buf.size(),
                             &written_len);
        COUNTER_UPDATE(_bytes_written_counter, buf.size());
    }
    COUNTER_UPDATE(_rows_written_counter, num_rows);
    return Status::OK;
}

Status ExportSink::gen_row_buffer(TupleRow* row, std::stringstream* ss) {
    int num_columns = _output_expr_ctxs.size();
    // const TupleDescriptor& desc = row_desc().TupleDescriptor;
    for (int i = 0; i < num_columns; ++i) {
        void* item = _output_expr_ctxs[i]->get_value(row);
        if (item == nullptr) {
            (*ss) << "NULL";
            continue;
        }
        switch (_output_expr_ctxs[i]->root()->type().type) {
        case TYPE_BOOLEAN:
        case TYPE_TINYINT:
            (*ss) << (int)*static_cast<int8_t*>(item);
            break;
        case TYPE_SMALLINT:
            (*ss) << *static_cast<int16_t*>(item);
            break;
        case TYPE_INT:
            (*ss) << *static_cast<int32_t*>(item);
            break;
        case TYPE_BIGINT:
            (*ss) << *static_cast<int64_t*>(item);
            break;
        case TYPE_LARGEINT:
            (*ss) << *static_cast<__int128*>(item);
            break;
        case TYPE_FLOAT:
            (*ss) << *static_cast<float*>(item);
            break;
        case TYPE_DOUBLE:
            (*ss) << *static_cast<double*>(item);
            break;
        case TYPE_DATE:
        case TYPE_DATETIME: {
            char buf[64];
            const DateTimeValue* time_val = (const DateTimeValue*)(item);
            time_val->to_string(buf);
            (*ss) << buf;
            break;
        }
        case TYPE_VARCHAR:
        case TYPE_CHAR: {
            const StringValue* string_val = (const StringValue*)(item);

            if (string_val->ptr == NULL) {
                if (string_val->len == 0) {
                } else {
                    (*ss) << "NULL";
                }
            } else {
                (*ss) << std::string(string_val->ptr, string_val->len);
            }
            break;
        }
        case TYPE_DECIMAL: {
            const DecimalValue* decimal_val = reinterpret_cast<const DecimalValue*>(item);
            std::string decimal_str;
            int output_scale = _output_expr_ctxs[i]->root()->output_scale();

            if (output_scale > 0 && output_scale <= 30) {
                decimal_str = decimal_val->to_string(output_scale);
            } else {
                decimal_str = decimal_val->to_string();
            }
            (*ss) << decimal_str;
            break;
        }
        default: {
            std::stringstream err_ss;
            err_ss << "can't export this type. type = " << _output_expr_ctxs[i]->root()->type();
            return Status(err_ss.str());
        }
        }
        if (i < num_columns - 1) {
            (*ss) << _t_export_sink.column_separator;
        }
    }
    (*ss) << _t_export_sink.line_delimiter;

    return Status::OK;
}

Status ExportSink::close(RuntimeState* state, Status exec_status) {
    Expr::close(_output_expr_ctxs, state);
    if (_file_writer != nullptr) {
        _file_writer->close();
        _file_writer = nullptr;
    }
    return Status::OK;
}

Status ExportSink::open_file_writer() {
    if (_file_writer != nullptr) {
        return Status::OK;
    }

    std::string file_name = gen_file_name();

    // TODO(lingbin): gen file path
    switch (_t_export_sink.file_type) {
    case TFileType::FILE_LOCAL: {
        LocalFileWriter* file_writer
                = new LocalFileWriter(_t_export_sink.export_path + "/" + file_name, 0);
        RETURN_IF_ERROR(file_writer->open());
        _file_writer.reset(file_writer);
        break;
    }
    case TFileType::FILE_BROKER: {
        BrokerWriter* broker_writer = new BrokerWriter(_state,
                                                       _t_export_sink.broker_addresses,
                                                       _t_export_sink.properties,
                                                       _t_export_sink.export_path + "/" + file_name,
                                                       0 /* offset */);
        RETURN_IF_ERROR(broker_writer->open());
        _file_writer.reset(broker_writer);
        break;
    }
    default: {
        std::stringstream ss;
        ss << "Unknown file type, type=" << _t_export_sink.file_type;
        return Status(ss.str());
    }
    }

    _state->add_export_output_file(_t_export_sink.export_path + "/" + file_name);
    return Status::OK;
}

// TODO(lingbin): add some other info to file name, like partition
std::string ExportSink::gen_file_name() {
    const TUniqueId& id = _state->fragment_instance_id();
    std::stringstream file_name;
    file_name << "export_data_" << id.hi << "_" << id.lo;
    return file_name.str();
}

}
