#ifndef HTTP_IMAGE_RETRIEVER_CONFIG_H
#define HTTP_IMAGE_RETRIEVER_CONFIG_H

// Public-safe include shim:
// - keep secrets in HttpImageRetrieverConfig.local.h (gitignored)
// - commit HttpImageRetrieverConfig.example.h for public/shared usage
#if defined(__has_include)
#if __has_include("HttpImageRetrieverConfig.local.h")
#include "HttpImageRetrieverConfig.local.h"
#else
#include "HttpImageRetrieverConfig.example.h"
#endif
#else
#include "HttpImageRetrieverConfig.example.h"
#endif

#endif
