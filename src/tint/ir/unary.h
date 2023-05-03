// Copyright 2023 The Tint Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_TINT_IR_UNARY_H_
#define SRC_TINT_IR_UNARY_H_

#include "src/tint/ir/instruction.h"
#include "src/tint/utils/castable.h"
#include "src/tint/utils/string_stream.h"

namespace tint::ir {

/// An instruction in the IR.
class Unary : public utils::Castable<Unary, Instruction> {
  public:
    /// The kind of instruction.
    enum class Kind {
        kAddressOf,
        kComplement,
        kIndirection,
        kNegation,
        kNot,
    };

    /// Constructor
    /// @param id the instruction id
    /// @param kind the kind of unary instruction
    /// @param type the result type
    /// @param val the lhs of the instruction
    Unary(uint32_t id, Kind kind, const type::Type* type, Value* val);
    Unary(const Unary& inst) = delete;
    Unary(Unary&& inst) = delete;
    ~Unary() override;

    Unary& operator=(const Unary& inst) = delete;
    Unary& operator=(Unary&& inst) = delete;

    /// @returns the kind of instruction
    Kind GetKind() const { return kind_; }

    /// @returns the value for the instruction
    const Value* Val() const { return val_; }

    /// Write the instruction to the given stream
    /// @param out the stream to write to
    /// @returns the stream
    utils::StringStream& ToInstruction(utils::StringStream& out) const override;

  private:
    Kind kind_;
    Value* val_ = nullptr;
};

}  // namespace tint::ir

#endif  // SRC_TINT_IR_UNARY_H_
