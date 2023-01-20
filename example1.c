// Copyright (c) 2022  Buzz Moschetti <buzz.moschetti@gmail.com>
// 
// Permission to use, copy, modify, and distribute this software and its documentation for any purpose, without fee, and without a written agreement is hereby granted,
// provided that the above copyright notice and this paragraph and the following two paragraphs appear in all copies.
// 
// IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, 
// ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
// THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE AUTHOR HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

#include <stdio.h>

#include <sqlite3.h>

#include <bson.h>


static void do_fetch(sqlite3 *db, const char* sql) {
    sqlite3_stmt* stmt = 0;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0 );
    if(rc != SQLITE_OK) {
	printf("ERROR prep rc: %d\n", rc);
	return;
    }
    
    while ( sqlite3_step( stmt ) == SQLITE_ROW ) { // While query has
						   // result-rows.
	// This makes sure we don't accidentally pick up an int AND
	// it also weeds out nulls (SQLITE_NULL):
	switch(sqlite3_column_type(stmt,0)) {
	case SQLITE_NULL: {
	    // do nothing
	    break;
	}
	case SQLITE_BLOB: {
	    const void* data = sqlite3_column_blob(stmt, 0);
	    int len = sqlite3_column_bytes(stmt, 0);
	    
	    bson_t b;
	    bson_init_static(&b, data, len);
	    
	    // As this point we have a BSON object (bson_t).  We
	    // could call all sorts of bson_get_whatever on it but
	    // for the moment 
			   size_t jsz;
	    printf("BSON: %s\n", bson_as_canonical_extended_json (&b, &jsz));
	    break;
	}
	case SQLITE_TEXT: {
	    const unsigned char* v = sqlite3_column_text(stmt, 0);
	    printf("STR: %s\n", v);
	    // apparently, no need to sqlite3_free(v)....
	    break;	    
	}
	case SQLITE_INTEGER: {
	    int v = sqlite3_column_int(stmt, 0);
	    printf("INT: %d\n", v);
	    break;	    
	}
	case SQLITE_FLOAT: {
	    double v = sqlite3_column_double(stmt, 0);
	    printf("DBL: %f\n", v);
	    break;	    
	}	    
	}
    }
    
    //  Step, Clear and Reset the statement after each bind.
    rc = sqlite3_clear_bindings( stmt );
    rc = sqlite3_reset( stmt );
    rc = sqlite3_finalize( stmt );  //  Finalize the prepared stat
}



static void insert(sqlite3 *db, int nn) {
    sqlite3_stmt* stmt = 0;

    char jbuf[256];

    //
    //  You can create BSON from scratch but for the purposes of this
    //  example we will just make it from some Extended JSON
    //
    sprintf(jbuf, "{\"hdr\":{\"id\":\"A%d\", \"ts\":{\"$date\":\"2023-01-12T13:14:15.678Z\"}}, \"amt\":{\"$numberDecimal\":\"10.09\"},  \"A\":{\"B\":[ 7 ,{\"X\":\"QQ\", \"Y\":[\"ee\",\"ff\"]}, 3.14159  ]} }", nn);

    bson_error_t err; // on stack
    bson_t* b = bson_new_from_json((const uint8_t *)jbuf, strlen(jbuf), &err);

    //
    //  Now, get the raw bytes from the BSON object to save as a BLOB:
    //
    const uint8_t* data = bson_get_data(b);
    int32_t len = b->len;

    int rc = sqlite3_prepare_v2(db, "INSERT INTO FOO (bdata) values (?)", -1, &stmt, 0 );
    if(rc != SQLITE_OK) {
	printf("ERROR prep rc: %d\n", rc);
	return;
    } 
    
    rc = sqlite3_bind_blob( stmt, 1, (const void*) data, len, SQLITE_STATIC);
    if(rc != SQLITE_OK) {
	printf("ERROR: bind rc: %d\n", rc);
	return;
    }

    // Doing an insert means we expect a single result DONE back:
    rc = sqlite3_step( stmt );
    if(rc != SQLITE_DONE) {
	printf("? INSERT yields rc %d\n", rc);
    }
	
    rc = sqlite3_clear_bindings( stmt );
    rc = sqlite3_reset( stmt );
    rc = sqlite3_finalize( stmt );  //  Finalize the prepared stat
}

static void create(sqlite3 *db) {
    sqlite3_stmt* stmt = 0;

    // Note we can call the column type something other than the other
    // official sqlite types:
    char* sql = "create table if NOT EXISTS FOO (bdata BSON)";    

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0 );
    if(rc != SQLITE_OK) {
	printf("ERROR prep rc: %d\n", rc);
	return;
    } 
    
    // Doing an insert means we expect a single result DONE back:
    rc = sqlite3_step( stmt );
    if(rc != SQLITE_DONE) {
	printf("? INSERT yields rc %d\n", rc);
    }
    rc = sqlite3_clear_bindings( stmt );
    rc = sqlite3_reset( stmt );
    rc = sqlite3_finalize( stmt );  //  Finalize the prepared stat
}



/*
  usage:  example1 someFile.sqlite3
 */
int main(int argc, char* argv[]) {
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;

    char* dbf = argv[1];
   
    rc = sqlite3_open(dbf, &db);

    if( rc ) {
	fprintf(stderr, "cannot open [%s]: %s\n", dbf, sqlite3_errmsg(db));
	return(0);
    } else {
	fprintf(stderr, "Opened [%s] successfully\n", dbf);
    }

    const char* ext_path = "bsonext";
    const char* entry_point = "sqlite3_bson_init";

    rc = sqlite3_enable_load_extension(db, 1); // TRUE
    if(rc != SQLITE_OK) {
	printf("cannot enable extension loading");
    } else {
	zErrMsg = 0;    
	rc = sqlite3_load_extension(db, ext_path, entry_point, &zErrMsg);
	if(rc != SQLITE_OK) {
	    printf("load ext [%s] failed: %d: %s\n", ext_path, rc, zErrMsg);
	    sqlite3_free(zErrMsg);
	}
    }

    // If you don't want to keep blasting the DB, you can comment
    // these out:
    create(db);
    for(int i = 0; i < 3; i++) { insert(db, i); }

    // Returns binary BLOB
    do_fetch(db, "select bdata from FOO");

    // Returns the hdr substructure but as JSON string:
    do_fetch(db, "select bson_get(bdata,\"hdr\") from FOO");

    // Returns the hdr substructure as binary BSON
    do_fetch(db, "select bson_get_bson(bdata,\"hdr\") from FOO");
    
    // Scalars get proper type:
    do_fetch(db, "select bson_get(bdata,\"hdr.id\") from FOO");

    // ...including digging thru an array to get a double at idx 2
    // in array A.B (indexes are zero based):
    do_fetch(db, "select bson_get(bdata,\"A.B.2\") from FOO");

    // Extension functions work in predicates, too.
    // Here, asking for "amt" which is a decimal128 will yield a 
    // STRING to avoid floating pt issues:
    do_fetch(db, "select bson_get(bdata,\"amt\") from FOO where bson_get(bdata, \"hdr.id\") = \"A2\"");

    // If we add some math into the expression, sqlite will 
    // Here, asking for "amt" which is a decimal128 will cause sqlite to
    // autoconvert the string to float and yield a float result:
    do_fetch(db, "select 11.6 + bson_get(bdata,\"amt\") from FOO where bson_get(bdata, \"hdr.id\") = \"A2\"");                
    
    // You can also use extension functions in views to pull commonly used
    // fields "up" to the top level to simply queries:
    //   CREATE VIEW easy_foo as
    //     select bson_get(bdata, "hdr.id") as ID, bson_get(bdata, "A.B.0") as
    //   code, bdata from foo;
    //
    // then
    //   select * from easy_foo where id = "A1";

    
    sqlite3_close(db);
}





