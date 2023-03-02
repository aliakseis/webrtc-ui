/*
 * This file is part of the sse package, copyright (c) 2011, 2012, @radiospiel.
 * It is copyrighted under the terms of the modified BSD license, see LICENSE.BSD.
 *
 * For more information see https://https://github.com/radiospiel/sse.
 */
 
#include "http.h"

#include <thread>
#include <chrono>
#include <mutex>

#define SSE_CLIENT_VERSION       "0.2"
#define SSE_CLIENT_USERAGENT     "sse/" SSE_CLIENT_VERSION


Options options{};

static char curl_error_buf[CURL_ERROR_SIZE];

static bool curl_perform(CURL* curl) {
  int retries = 5;
  while(1) {
    CURLcode res = curl_easy_perform(curl);

    switch(res) {
      case CURLE_OK:
      case CURLE_ABORTED_BY_CALLBACK:
        return true;
      case CURLE_COULDNT_RESOLVE_PROXY:
      case CURLE_COULDNT_RESOLVE_HOST:
      case CURLE_COULDNT_CONNECT:
        fprintf(stderr, "curl: %s\n", curl_error_buf);
        if(retries-- <= 0) 
          return false;

        fprintf(stderr, "retrying...\n");
        std::this_thread::sleep_for(std::chrono::seconds(3));
        break;
      default:
        fprintf(stderr, "curl: %s\n", curl_error_buf);
        return false;
    }
  }

  return true;
}

static void curl_log_result(CURL* curl) {
  typedef const char* string;

  #define DEFAULT_string  ""
  #define DEFAULT_long    0
  #define DEFAULT_double  0
  #define FORMAT_string   "%s"
  #define FORMAT_long     "%ld"
  #define FORMAT_double   "%f"

#define curl_info(T, INFO) T INFO; curl_easy_getinfo(curl, CURLINFO_ ## INFO, &INFO); if(!INFO) INFO = DEFAULT_ ## T

#define log_curl_info(T, INFO)  curl_info(T, INFO); \
                                if(options.verbosity >= 2) { \
                                  fprintf(stderr, "%26s: " FORMAT_ ## T "\n", #INFO, INFO); \
                                }

  log_curl_info(string,  EFFECTIVE_URL);
  log_curl_info(long,    RESPONSE_CODE);
  log_curl_info(long,    FILETIME);
  log_curl_info(double,  TOTAL_TIME);
  log_curl_info(double,  NAMELOOKUP_TIME);
  log_curl_info(double,  CONNECT_TIME);
  log_curl_info(double,  APPCONNECT_TIME);
  log_curl_info(double,  PRETRANSFER_TIME);
  log_curl_info(double,  STARTTRANSFER_TIME);
  log_curl_info(double,  REDIRECT_TIME);
  log_curl_info(long,    REDIRECT_COUNT);
  log_curl_info(double,  SIZE_UPLOAD);
  log_curl_info(double,  SIZE_DOWNLOAD);
  log_curl_info(double,  SPEED_DOWNLOAD);
  log_curl_info(double,  SPEED_UPLOAD);
  log_curl_info(long,    HEADER_SIZE);
  log_curl_info(long,    REQUEST_SIZE);
  log_curl_info(long,    SSL_VERIFYRESULT);
  log_curl_info(double,  CONTENT_LENGTH_DOWNLOAD);
  log_curl_info(double,  CONTENT_LENGTH_UPLOAD);
  log_curl_info(string,  CONTENT_TYPE);
  log_curl_info(long,    NUM_CONNECTS);
  log_curl_info(string,  PRIMARY_IP);
  log_curl_info(long,    PRIMARY_PORT);
  log_curl_info(string,  LOCAL_IP);
  log_curl_info(long,    LOCAL_PORT);

#undef log_curl_info
#undef curl_info
}

/*
 * returns a prepared curl handle.
 *
 * Note that we always work with the same curl handle. This seems to work great,
 * and it means we don't have to clean up handles, because all we leak is just
 * that one handle, and it is going to be reused all the time anyways.
 *
 * THIS ALSO MEANS THAT THIS CODE IS NOT THREADSAFE, and propably NOT REENTRANT.
 */
//#define MAX_HANDLES 10

static CURL* curl_handle(int /*index*/) {
  static std::once_flag curl_initialised;

  std::call_once(curl_initialised,[] {
    curl_global_init(CURL_GLOBAL_ALL);  /* In windows, this will init the winsock stuff */ 
    atexit(curl_global_cleanup);
  });

  CURL* curl = curl_easy_init();
  if (!curl)
      return nullptr;

  /* === verbosity? ================================================ */

  curl_easy_setopt(curl, CURLOPT_VERBOSE, options.verbosity >= 1 ? 1 : 0);

  /* === set defaults ============================================== */

  curl_easy_setopt(curl, CURLOPT_USERAGENT, SSE_CLIENT_USERAGENT);

  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error_buf);
  
  /* === allow insecure connections? =============================== */

  /*
   * If you want to connect to a site who isn't using a certificate 
   * that is signed by one of the certs in the CA bundle you have, you
   * can skip the verification of the server's certificate. This makes 
   * the connection A LOT LESS SECURE.
   *
   * If you have a CA cert for the server stored someplace else than 
   * in the default bundle, then the CURLOPT_CAPATH option might come 
   * handy for you.
   */ 
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, options.allow_insecure ? 0L : 1L);

  /*
   * If the site you're connecting to uses a different host name that 
   * what they have mentioned in their server certificate's commonName
   * (or subjectAltName) fields, libcurl will refuse to connect. You can 
   * skip this check, but this will make the connection less secure.
   */ 
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, options.allow_insecure ? 0L : 1L);

  /* === set certificates? ========================================= */
  
  /*
   * Did the user request a specific set of certifications?
   */
  if(options.ssl_cert)
    curl_easy_setopt(curl, CURLOPT_SSLCERT, options.ssl_cert);
  
  if(options.ca_info) 
    curl_easy_setopt(curl, CURLOPT_CAINFO, options.ca_info);

  // https://stackoverflow.com/questions/30098087/is-libcurl-really-thread-safe
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
  
  return curl;
}


static size_t onData(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    OnDataFunc& onDataFunc = *static_cast<OnDataFunc*>(userdata);
    return onDataFunc(ptr, size, nmemb);
}

static size_t progressCallback(void *clientp, // https://curl.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html
    curl_off_t dltotal,
    curl_off_t dlnow,
    curl_off_t ultotal,
    curl_off_t ulnow)
{
    OnProgressFunc& onProgressFunc = *static_cast<OnProgressFunc*>(clientp);
    return onProgressFunc(dltotal,
        dlnow,
        ultotal,
        ulnow);
}

bool http(HttpVerb verb,
  const char*   url, 
  const char**  http_headers, 
  
  const char*   body, 
  unsigned      bodyLenght,
  
  OnDataFunc    on_data,
  std::function<const char*(CURL*)> on_verify,
  OnProgressFunc progress_callback)
{
  CURL *curl = curl_handle(verb);
  if (!curl)
      return false;

  // -- set URL -------------------------------------------------------
  
  curl_easy_setopt(curl, CURLOPT_URL, url);

  // -- set headers ---------------------------------------------------
  
  struct curl_slist *headers = NULL;
  while(http_headers && *http_headers)
    headers = curl_slist_append(headers, *http_headers++);

  if(verb == HTTP_POST) {
    /*
     * HTTP 1.1 specifies that a POST should use a "Expect: 100-continue" 
     * header. This should prepare the server for a potentially large 
     * upload.
     *
     * Our bodies are not that big anyways, and some servers - notably thin
     * running standalone does not send these - leading to a one or two-seconds
     * delay. 
     *
     * Therefore we disable this behaviour.
     */
    headers = curl_slist_append(headers, "Expect:");
  }
  
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 
  
  // -- set body ------------------------------------------------------
  
  if(verb == HTTP_POST) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);                     /* set POST */ 
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);             /* set POST data */
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, bodyLenght);
  }
  
  // -- set write function --------------------------------------------
  
  if (on_data) {
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&on_data));
  }

  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, !progress_callback);
  if (progress_callback) {
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, static_cast<void*>(&progress_callback));
  }
  
  // -- perform -------------------------------------------------------
  
  bool result = curl_perform(curl);         /* Perform the request */ 

  // -- log CURL result -----------------------------------------------

  if(options.verbosity >= 2)
    curl_log_result(curl);

  // -- verify status code --------------------------------------------

  long response_code; 
  const char* effective_url = 0;
  
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code); 
  if(response_code < 200 || response_code >= 300) {
    if(!effective_url)
      curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url); 
    
    fprintf(stderr, "%s: HTTP(S) status code %ld\n", effective_url, response_code);
    result = false;
  }

  // -- verify response. exit if verification fails -------------------
  
  const char* verification_error = on_verify ? on_verify(curl) : 0;
  if(verification_error) {
    if(!effective_url)
      curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url); 

    fprintf(stderr, "%s: %s\n", effective_url, verification_error);
    result = false;
  }
  
  // -- cleanup -------------------------------------------------------
  
  if(headers)
    curl_slist_free_all(headers);

  // We clean up.
  curl_easy_cleanup(curl);    /* cleanup */

  return  result;
}
