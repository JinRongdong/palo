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

#include "runtime/types.h"

#include <ostream>
#include <sstream>
#include <boost/foreach.hpp>

#include "codegen/llvm_codegen.h"

namespace palo {

const char* TypeDescriptor::s_llvm_class_name = "struct.palo::TypeDescriptor";

TypeDescriptor::TypeDescriptor(const std::vector<TTypeNode>& types, int* idx) : 
        len(-1), precision(-1), scale(-1) {
    DCHECK_GE(*idx, 0);
    DCHECK_LT(*idx, types.size());
    const TTypeNode& node = types[*idx];
    switch (node.type) {
    case TTypeNodeType::SCALAR: {
        DCHECK(node.__isset.scalar_type);
        const TScalarType scalar_type = node.scalar_type;
        type = thrift_to_type(scalar_type.type);
        if (type == TYPE_CHAR || type == TYPE_VARCHAR || type == TYPE_HLL) {
            DCHECK(scalar_type.__isset.len);
            len = scalar_type.len;
        } else if (type == TYPE_DECIMAL) {
            DCHECK(scalar_type.__isset.precision);
            DCHECK(scalar_type.__isset.scale);
            precision = scalar_type.precision;
            scale = scalar_type.scale;
        }
        break;
    }
#if 0 // Don't support now
    case TTypeNodeType::STRUCT:
        type = TYPE_STRUCT;
        for (int i = 0; i < node.struct_fields.size(); ++i) {
            ++(*idx);
            children.push_back(TypeDescriptor(types, idx));
            field_names.push_back(node.struct_fields[i].name);
        }
        break;
    case TTypeNodeType::ARRAY:
        DCHECK(!node.__isset.scalar_type);
        DCHECK_LT(*idx, types.size() - 1);
        type = TYPE_ARRAY;
        ++(*idx);
        children.push_back(TypeDescriptor(types, idx));
        break;
    case TTypeNodeType::MAP:
        DCHECK(!node.__isset.scalar_type);
        DCHECK_LT(*idx, types.size() - 2);
        type = TYPE_MAP;
        ++(*idx);
        children.push_back(TypeDescriptor(types, idx));
        ++(*idx);
        children.push_back(TypeDescriptor(types, idx));
        break;
#endif
    default:
        DCHECK(false) << node.type;
    }
}

void TypeDescriptor::to_thrift(TTypeDesc* thrift_type) const {
    thrift_type->types.push_back(TTypeNode());
    TTypeNode& node = thrift_type->types.back();
    if (is_complex_type()) {
        if (type == TYPE_ARRAY) {
            node.type = TTypeNodeType::ARRAY;
        } else if (type == TYPE_MAP) {
            node.type = TTypeNodeType::MAP;
        } else {
            DCHECK_EQ(type, TYPE_STRUCT);
            node.type = TTypeNodeType::STRUCT;
            node.__set_struct_fields(std::vector<TStructField>());
            for (auto& field_name : field_names) {
                node.struct_fields.push_back(TStructField());
                node.struct_fields.back().name = field_name;
            }
        }
        BOOST_FOREACH(const TypeDescriptor& child, children) {
            child.to_thrift(thrift_type);
        }
    } else {
        node.type = TTypeNodeType::SCALAR;
        node.__set_scalar_type(TScalarType());
        TScalarType& scalar_type = node.scalar_type;
        scalar_type.__set_type(palo::to_thrift(type));
        if (type == TYPE_CHAR || type == TYPE_VARCHAR || type == TYPE_HLL) {
            // DCHECK_NE(len, -1);
            scalar_type.__set_len(len);
        } else if (type == TYPE_DECIMAL) {
            DCHECK_NE(precision, -1);
            DCHECK_NE(scale, -1);
            scalar_type.__set_precision(precision);
            scalar_type.__set_scale(scale);
        }
    }
}

std::string TypeDescriptor::debug_string() const {
    std::stringstream ss;
    switch (type) {
    case TYPE_CHAR:
        ss << "CHAR(" << len << ")";
        return ss.str();
    case TYPE_DECIMAL:
        ss << "DECIMAL(" << precision << ", " << scale << ")";
        return ss.str();
    default:
        return type_to_string(type);
    }
}

std::ostream& operator<<(std::ostream& os, const TypeDescriptor& type) {
  os << type.debug_string();
  return os;
}

llvm::ConstantStruct* TypeDescriptor::to_ir(LlvmCodeGen* codegen) const {
    // ColumnType = { i32, i32, i32, i32, <vector>, <vector> }
    llvm::StructType* column_type_type = llvm::cast<llvm::StructType>(
        codegen->get_type(s_llvm_class_name));

    DCHECK_EQ(sizeof(type), sizeof(int32_t));
    llvm::Constant* type_field = llvm::ConstantInt::get(codegen->int_type(), type);
    DCHECK_EQ(sizeof(len), sizeof(int32_t));
    llvm::Constant* len_field = llvm::ConstantInt::get(codegen->int_type(), len);
    DCHECK_EQ(sizeof(precision), sizeof(int32_t));
    llvm::Constant* precision_field = llvm::ConstantInt::get(codegen->int_type(), precision);
    DCHECK_EQ(sizeof(scale), sizeof(int32_t));
    llvm::Constant* scale_field = llvm::ConstantInt::get(codegen->int_type(), scale);

    // Create empty 'children' and 'field_names' vectors
    DCHECK(children.empty()) << "Nested types NYI";
    DCHECK(field_names.empty()) << "Nested types NYI";
    llvm::Constant* children_field = llvm::Constant::getNullValue(
        column_type_type->getElementType(4));
    llvm::Constant* field_names_field =
        llvm::Constant::getNullValue(column_type_type->getElementType(5));

    return llvm::cast<llvm::ConstantStruct>(llvm::ConstantStruct::get(
            column_type_type, type_field, len_field,
            precision_field, scale_field, children_field, field_names_field, NULL));
}

}

