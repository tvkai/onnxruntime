// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifndef USE_ONNXRUNTIME_DLL
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <google/protobuf/message_lite.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#endif

#include "gtest/gtest.h"
#include "core/session/onnxruntime_cxx_api.h"
#include "core/session/abi_session_options_impl.h"

std::unique_ptr<Ort::Env> ort_env;

void ortenv_setup(){
  OrtThreadingOptions tpo;
  ort_env.reset(new Ort::Env(&tpo, ORT_LOGGING_LEVEL_WARNING, "Default"));
}

TEST(TestSessionOptions, SetIntraOpNumThreadsWithoutEnv) {
  Ort::SessionOptions session_options;
  session_options.SetIntraOpNumThreads(48);
  const auto* ort_session_options = (const OrtSessionOptions*)session_options;
  ASSERT_EQ(ort_session_options->value.intra_op_param.thread_pool_size, 48);
}

int main(int argc, char** argv) {
  int status = 0;
  ortenv_setup();
  ORT_TRY {
    ::testing::InitGoogleTest(&argc, argv);
    status = RUN_ALL_TESTS();
  }
  ORT_CATCH(const std::exception& ex) {
    ORT_HANDLE_EXCEPTION([&]() {
      std::cerr << ex.what();
      status = -1;
    });
  }

  //TODO: Fix the C API issue
  ort_env.reset();  //If we don't do this, it will crash          

#ifndef USE_ONNXRUNTIME_DLL
  //make memory leak checker happy
  ::google::protobuf::ShutdownProtobufLibrary();
#endif
  return status;
}
