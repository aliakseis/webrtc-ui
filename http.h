/*
 * This file is part of the sse package, copyright (c) 2011, 2012, @radiospiel.
 * It is copyrighted under the terms of the modified BSD license, see LICENSE.BSD.
 *
 * For more information see https://https://github.com/radiospiel/sse.
 */

#ifndef HTTP_H
#define HTTP_H

#include <string.h>
#include <curl/curl.h>

#include <functional>

/*
 * send HTTP request.
 */

enum HttpVerb {
    HTTP_GET = 0,
    HTTP_POST = 1,
};

typedef std::function<size_t(char*, size_t, size_t)> OnDataFunc;

typedef std::function<size_t(curl_off_t, curl_off_t, curl_off_t, curl_off_t)> OnProgressFunc;

bool http(HttpVerb  verb,
  const char*   url, 
  const char**  http_headers, 

  const char*   body, 
  unsigned      bodyLenght,

  OnDataFunc    on_data = {},
  std::function<const char*(CURL*)> on_verify = {},
  OnProgressFunc progress_callback = {}
);


/*
 * Aplication options
 */
struct Options {
    const char *arg0;           // process name
    int         limit;          // event limit
    int         verbosity;      // verbosity
    int         allow_insecure; // allow insecure connections
    const char *ssl_cert;       // SSL cert file
    const char *ca_info;        // CA cert file
};

extern Options options;

#endif
