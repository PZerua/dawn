// Copyright 2022 The Tint Authors.
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

#ifndef SRC_TINT_FUZZERS_SHUFFLE_TRANSFORM_H_
#define SRC_TINT_FUZZERS_SHUFFLE_TRANSFORM_H_

#include "src/tint/ast/transform/transform.h"

namespace tint::fuzzers {

/// ShuffleTransform reorders the module scope declarations into a random order
class ShuffleTransform : public ast::transform::Transform {
  public:
    /// Constructor
    /// @param seed the random seed to use for the shuffling
    explicit ShuffleTransform(size_t seed);

    /// @copydoc ast::transform::Transform::Apply
    ApplyResult Apply(const Program* program,
                      const ast::transform::DataMap& inputs,
                      ast::transform::DataMap& outputs) const override;

  private:
    size_t seed_;
};

}  // namespace tint::fuzzers

#endif  // SRC_TINT_FUZZERS_SHUFFLE_TRANSFORM_H_
