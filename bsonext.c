// Copyright (c) 2022-2024  Buzz Moschetti <buzz.moschetti@gmail.com>
// 
// Permission to use, copy, modify, and distribute this software and its documentation for any purpose, without fee, and without a written agreement is hereby granted,
// provided that the above copyright notice and this paragraph and the following two paragraphs appear in all copies.
// 
// IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, 
// ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
// THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE AUTHOR HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <assert.h>
#include <string.h>

#include "bson.h"  // obviously...


static void _cvt_datetime_to_ts(char* buf, int64_t millis_since_epoch)
{
    time_t t_unix = millis_since_epoch/1000; // get seconds..
    unsigned t_ms = millis_since_epoch%1000; // ... and the millis...

    struct tm unix_tm = *gmtime(&t_unix); // * to deref and copy out internal buffer

    // BSON datetime is int64 number of millis (not seconds) since epoch
    // and by convention is a Z (UTC) zone
    sprintf(buf, "%4d-%02d-%02dT%02d:%02d:%02d.%03dZ",
	    unix_tm.tm_year + 1900,
	    unix_tm.tm_mon + 1,  // POSIX is 0-11
	    unix_tm.tm_mday,
	    unix_tm.tm_hour,
	    unix_tm.tm_min,
	    unix_tm.tm_sec,
	    t_ms);
}


static bool _init_bson(
    bson_t* b,
    sqlite3_value **argv
    )
{
/*
  From https://www.sqlite.org/c3ref/value_blob.html:
  Please pay particular attention to the fact that the pointer returned from
    sqlite3_value_blob(),
    sqlite3_value_text(),
    sqlite3_value_text16()
  **can** be invalidated by a subsequent call to
    sqlite3_value_bytes(),
    sqlite3_value_bytes16(),
    sqlite3_value_text(),
    sqlite3_value_text16().

  That **can** depends on if type conversion results from trying to get
  appropriate length of data pointer -- which is not an issue for
  BLOBs here.   Great explain at:
  https://sqlite.org/forum/info/5165df43db42e86f6dd027189340099174f24f553b157ecf6e7bc2e40224ff32
*/    
    const void* bson = sqlite3_value_blob(argv[0]);
    int bson_len = sqlite3_value_bytes(argv[0]);

    // This does a sort of OK job at sniffing the BSON to see if it
    // is OK....
    return bson_init_static(b, bson, bson_len);
}
    
static void _set_json(
    sqlite3_context *context,
    bson_t* b)
{
    size_t blen;
    char* txt = bson_as_relaxed_extended_json(b, &blen);
    sqlite3_result_text(context, txt, blen, SQLITE_TRANSIENT);
    bson_free((void*)txt);
}
    

static void bson_get_bson_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  assert( argc==2 );

  // If not a BLOB (also picks up if NULL) then don't even try to init:
  if( sqlite3_value_type(argv[0]) != SQLITE_BLOB) return;

  bson_t b; // on stack;
  if(!_init_bson(&b, argv)) {
      sqlite3_result_error(context, "invalid BSON", -1);
  } else {
      uint32_t subdoc_len;
      const uint8_t* subdoc_data = 0;

      char* dotpath = (char*) sqlite3_value_text(argv[1]);

      if(dotpath[0] == '\0') {
	  subdoc_len = b.len;
	  subdoc_data = bson_get_data(&b);

      } else {
	  bool rc = false;
	  
	  bson_iter_t target;
	  bson_iter_t iter;
	  if (bson_iter_init (&iter, &b)) {
	      rc = bson_iter_find_descendant(&iter, dotpath, &target);
	  }
	  
	  if(rc) {
	      bson_type_t ft = bson_iter_type(&target);
	      switch(ft) {
	      case BSON_TYPE_DOCUMENT:  {
		  bson_iter_document(&target, &subdoc_len, &subdoc_data);
		  break;
	      }
	      case BSON_TYPE_ARRAY:  {
		  bson_iter_array(&target, &subdoc_len, &subdoc_data);
		  break;
	      }
	      default: {
		  // ?  TBD How to "better" handle "object representation" of
		  // noncomplex types
	      }
	      }
	  }
      }

      if(subdoc_data != 0) {
	  // Everything up to this point was pointers to pointers to pointers
	  // in the incoming sqlite3_value_bytes(argv[0]); no new mallocs
	  // so nothing extra to free; let TRANSIENT copy it out and we're done
	  sqlite3_result_blob(context, subdoc_data, subdoc_len, SQLITE_TRANSIENT);	  
      }
  }
}




static void extract_and_set_context(
  sqlite3_context* context,
  bson_iter_t* p_target
){
    bson_type_t ft = bson_iter_type(p_target);
    switch(ft) {
    case BSON_TYPE_UTF8: {
	uint32_t len;		
	const char* txt = bson_iter_utf8(p_target, &len); // NO NEED TO free()
	// DEF need TRANSIENT here to copy that string...
	sqlite3_result_text(context, txt, len, SQLITE_TRANSIENT);
	break;
    }
    case BSON_TYPE_DOUBLE: {
	double v = bson_iter_double(p_target);
	sqlite3_result_double(context, v);	      
	break;		
    }
    case BSON_TYPE_INT32: {
	int32_t v = bson_iter_int32(p_target);
	sqlite3_result_int(context, v);	      
	break;		
    }
    case BSON_TYPE_INT64: {
	int64_t v = bson_iter_int64(p_target);
	sqlite3_result_int64(context, v);
	break;		
    }
    case BSON_TYPE_DECIMAL128: {
	bson_decimal128_t val;
	if(bson_iter_decimal128(p_target, &val)) {
	    // No decimal type in sqlite and it is dangerous to
	    // use floating pt for penny-precise numbers so safest
	    // to turn it into a string:
	    char buf[43];  // from bson.h...  
	    bson_decimal128_to_string(&val, buf);
	    // -1 means let sqlite figure out length
	    sqlite3_result_text(context, buf, -1, SQLITE_TRANSIENT);
	}
	break;		
    }
    case BSON_TYPE_DATE_TIME: {
	int64_t millis_since_epoch = bson_iter_date_time (p_target);
	
	// No date/time/datetime in sqlite so make an ISO-8601 string.
	// BSON datetime is always Z
	// 2023-01-01T12:13:14.567Z  is 24 chars.
	char buf[24+1];
	_cvt_datetime_to_ts(buf, millis_since_epoch);
	sqlite3_result_text(context, buf, 24, SQLITE_TRANSIENT);
	break;		
    }								
	
    case BSON_TYPE_DOCUMENT: 
    case BSON_TYPE_ARRAY: {		
	uint32_t subdoc_len;
	const uint8_t* subdoc_data;
	
	if(ft == BSON_TYPE_DOCUMENT) {
	    bson_iter_document(p_target, &subdoc_len, &subdoc_data);
	} else {
	    bson_iter_array(p_target, &subdoc_len, &subdoc_data);
	}
	
	bson_t b; // on stack
	bson_init_static(&b, subdoc_data, subdoc_len);

	_set_json(context, &b);	

	break;
    }
	
    case BSON_TYPE_BINARY: {				
	bson_subtype_t subtype;
	uint32_t len;
	const uint8_t* data;
	
	// What to do with subtype?  Dunno!
	bson_iter_binary (p_target, &subtype, &len, &data);
	
	// sqlite likes binary hex input in this form ; here is [hello!]:
	//    insert into zzz (f) values (x'68656c6c6f21');
	// So given the convention that the output representation should
	// be whatever goes between single quotes, our rep will be
	//    68656c6c6f21 
	// No need for space for NULL; we will build a buf and know the
	// len to give to sqlite3_result_text:
	
	char* tmpp = sqlite3_malloc(len*2);
	
	int idx = 0;
	for(int n = 0; n < len; n++) {
	    // Love that pointer math....
	    sprintf(tmpp+idx, "%02x", (uint8_t)data[n]);
	    idx += 2;
	}
	
	// Since we are using sqlite3_malloc to make the buffer, we can use
	// sqlite_free as arg 4 to destroy tmpp:
	sqlite3_result_text(context, tmpp, idx, sqlite3_free);	
	break;
    }
	
    default: {
	break; // ?
    }		
    }
}

/*
sqlite does not (by default) enforce column types.  The data declares its
type, not the container.  This makes it vastly simpler to extract and
return data because the function can call sqlite3_result_int or
sqlite3_result_text or whatever else it wants.
 */
static void bson_get_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
    assert( argc==2 );

    // If not a BLOB (also picks up if NULL) then don't even try to init:
    if( sqlite3_value_type(argv[0]) != SQLITE_BLOB) return;

    bson_t b;
    if(!_init_bson(&b, argv)) {
	sqlite3_result_error(context, "invalid BSON", -1);
    } else {
	char* dotpath = (char*) sqlite3_value_text(argv[1]);

	if(dotpath[0] == '\0') { // bson_get(bson,"") is basically to_json()
	    _set_json(context, &b);
	
	} else {
	    bool rc = false;
	    bson_iter_t target;
	    bson_iter_t iter;
	    if (bson_iter_init (&iter, &b)) {
		rc = bson_iter_find_descendant(&iter, dotpath, &target);
	    }
	    if(rc) {
		extract_and_set_context(context, &target);
	    }
	}
    }
}

static void bson_to_json_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
    assert( argc==1 );

    // If not a BLOB (also picks up if NULL) then don't even try to init:
    if( sqlite3_value_type(argv[0]) != SQLITE_BLOB) return;

    bson_t b;
    if(!_init_bson(&b, argv)) {
	sqlite3_result_error(context, "invalid BSON", -1);
    } else {
	_set_json(context, &b);
    }
}


static void bson_from_json_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
    assert( argc==1 );

    char* jsons = (char*) sqlite3_value_text(argv[0]);
    int slen = strlen(jsons);

    bson_error_t err; // on stack
    bson_t* b = bson_new_from_json((const uint8_t *)jsons, slen, &err);
    
    if(b != NULL) {
	// b->len is The Official way to get length of bson_t* b.
	// SQLITE_TRANSIENT will safe-copy our data...
	sqlite3_result_blob(context, bson_get_data(b), b->len, SQLITE_TRANSIENT);
	// ...and now we must free it here with bson_destroy,
	// not free or sqlite3_free.  We also cannot pass bson_destroy as
	// arg 4 to sqlite3_result_blob
	bson_destroy(b);

	
    } else {
	sqlite3_result_error(context, "cannot parse EJSON", -1);	
    }
}


#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_bson_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */

  rc = sqlite3_create_function(db, "bson_get", 2,
                   SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                   0, bson_get_func, 0, 0);

  // Nice convenience; same as bson_get(bson_column, ""):
  rc = sqlite3_create_function(db, "bson_to_json", 1,
                   SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                   0, bson_to_json_func, 0, 0);  

  rc = sqlite3_create_function(db, "bson_get_bson", 2,
                   SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                   0, bson_get_bson_func, 0, 0);

  // Easier way to insert EJSON into BLOB column:
  rc = sqlite3_create_function(db, "bson_from_json", 1,
                   SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                   0, bson_from_json_func, 0, 0);  

  
  return rc;
}

