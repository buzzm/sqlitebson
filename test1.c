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

#include <stdio.h>

#include <sqlite3.h>

#include <bson.h>

char err_buf[1024]; // nice big err msg buffer...

static char* basic_changes_test(sqlite3 *db, const char* sql, int exp_changes)
{
    sqlite3_stmt* stmt = 0;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0 );
    if(rc != SQLITE_OK) {
	sprintf(err_buf, "sqlite3_prepare_v2 [%s]: rc: %d\n", sql, rc);
	return err_buf;
    }

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE) {
	sprintf(err_buf, "sqlite3_step [%s]: did not return DONE\n", sql);
	return err_buf;	
    }

    int act_changes = sqlite3_changes(db);
    if(act_changes != exp_changes) {    
	sprintf(err_buf, "sqlite3_changes [%s]: expect [%d], got [%d]\n", sql, exp_changes, act_changes);
	return err_buf;	
    }

    //  Step, Clear and Reset the statement after each bind.
    rc = sqlite3_clear_bindings( stmt );
    rc = sqlite3_reset( stmt );
    rc = sqlite3_finalize( stmt );  //  Finalize the prepared stat

    return 0;  // OK    
}

static void exec_bct(sqlite3 *db, const char* desc, const char* sql, int exp_changes)
{
    char* err;
    printf("%s ... ", desc);
    if((err = basic_changes_test(db, sql, exp_changes)) != NULL) {
	printf("FAIL; %s: [%s]\n", sql, err);
    } else {
	printf("ok\n");
    }	
}
    

static char* basic_scalar_test(sqlite3 *db, const char* sql, bson_type_t exp_type, const void* exp_value)
{
    sqlite3_stmt* stmt = 0;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0 );
    if(rc != SQLITE_OK) {
	sprintf(err_buf, "sqlite3_prepare_v2 [%s]: rc: %d\n", sql, rc);
	return err_buf;
    }

    int one_row_rc = sqlite3_step(stmt);
    if(one_row_rc == SQLITE_ROW && exp_type == BSON_TYPE_EOD) {
	sprintf(err_buf, "expected empty response but got at least 1");
	return err_buf;
    }

    {
	if(exp_type == BSON_TYPE_UTF8) {
	    const char* v = (const char*)sqlite3_column_text(stmt, 0);
	    //const unsigned char* v = sqlite3_column_text(stmt, 0);
	    //
	    if(v == NULL) {
		sprintf(err_buf, "expect [%s]; got [null]\n", (const char*)exp_value);
		return err_buf;
	    }	    
	    if(strcmp(v,exp_value)) {
		sprintf(err_buf, "expect [%s]; got [%s]\n", (const char*)exp_value, v);
		return err_buf;
	    }	    
	}
	if(exp_type == BSON_TYPE_DOUBLE) {
	    double v = *((double*)exp_value);
	    double v2 = sqlite3_column_double(stmt, 0);
	    //if(fabs(v - v2) > 0.000000001) {
	    if(v - v2 != 0.0) {
		sprintf(err_buf, "expect [%.10lf]; got [%.10lf]\n", v, v2);
		return err_buf;
	    }
	}
	if(exp_type == BSON_TYPE_INT32) {
	    int v = *((int*)exp_value);
	    int v2 = sqlite3_column_int(stmt, 0);
	    if(v != v2) {
		sprintf(err_buf, "expect [%d]; got [%d]\n", v, v2);
		return err_buf;
	    }
	}
	if(exp_type == BSON_TYPE_INT64) {
	    long v = *((long*)exp_value);
	    long v2 = sqlite3_column_int64(stmt, 0);
	    if(v != v2) {
		sprintf(err_buf, "expect [%ld]; got [%ld]\n", v, v2);
		return err_buf;
	    }
	}		
	
	if(exp_type == BSON_TYPE_NULL) {
	    int st = sqlite3_column_type(stmt,0);
	    if(SQLITE_NULL != st) {
		sprintf(err_buf, "expect NULL; got sqlite type %d", st);
		return err_buf;
	    }
	}	    
    }

    
    //  Step, Clear and Reset the statement after each bind.
    rc = sqlite3_clear_bindings( stmt );
    rc = sqlite3_reset( stmt );
    rc = sqlite3_finalize( stmt );  //  Finalize the prepared stat

    return 0;  // OK
}

static void exec_bst(sqlite3 *db, const char* desc, const char* sql, bson_type_t exp_type, const void* exp_value)
{
    char* err;
    printf("%s ... ", desc);
    if((err = basic_scalar_test(db, sql, exp_type, exp_value)) != NULL) {
	printf("FAIL; %s: [%s]\n", sql, err);
    } else {
	printf("ok\n");
    }	
}


static int insert(sqlite3 *db) {
    sqlite3_stmt* stmt = 0;

    char jbuf[512];
    char jbuf2[512];

    //
    //  You can create BSON from scratch but for the purposes of this
    //  example we will just make it from some Extended JSON.
    //
    //  That weird binary is:  Pretend this is a JPEG
    //
    sprintf(jbuf, "{\"hdr\":{\"id\":\"A%d\", \"ts\":{\"$date\":\"2023-01-12T13:14:15.678Z\"}, \"bigint\":{\"$numberLong\":\"743859238573\"}}, \"amt\":{\"$numberDecimal\":\"10.09\"},  \"A\":{\"B\":[ 7 ,{\"X\":\"QQ\", \"Y\":[\"ee\",\"ff\"]}, 3.14159  ]}, \"thumbnail\" : { \"$binary\" : { \"base64\" : \"UHJldGVuZCB0aGlzIGlzIGEgSlBFRw==\", \"subType\" : \"00\" }}  }", 0); // id:"A0"

    sprintf(jbuf2, "{\"hdr\":{\"id\":\"A%d\", \"ts\":{\"$date\":\"2023-01-12T13:14:15.678Z\"}, \"bigint\":{\"$numberLong\":\"743859238573\"}}, \"amt\":{\"$numberDecimal\":\"10.09\"},  \"A\":{\"B\":[ 7 ,{\"X\":\"QQ\", \"Y\":[\"ee\",\"ff\"]}, 3.14159  ]}, \"thumbnail\" : { \"$binary\" : { \"base64\" : \"UHJldGVuZCB0aGlzIGlzIGEgSlBFRw==\", \"subType\" : \"00\" }}  }", 3); // id:"A3"


    bson_error_t err; // on stack
    bson_t* b = bson_new_from_json((const uint8_t *)jbuf, strlen(jbuf), &err);
    if(b == NULL) {
	printf("ERROR bad JSON 1 upon insert");
	return 1;
    } 
    //  Now, get the raw bytes from the BSON object to save as a BLOB:
    const uint8_t* data = bson_get_data(b);
    int32_t len = b->len; // yes; there is no bson_get_len(b)

    b = bson_new_from_json((const uint8_t *)jbuf2, strlen(jbuf2), &err);
    if(b == NULL) {
	printf("ERROR bad JSON 2 upon insert");
	return 1;
    } 
    //  Now, get the raw bytes from the BSON object to save as a BLOB:
    const uint8_t* data2 = bson_get_data(b);
    int32_t len2 = b->len; // yes; there is no bson_get_len(b)    

    
    int rc = sqlite3_prepare_v2(db, "INSERT INTO bsontest (bdata,bdata2) values (?,?)", -1, &stmt, 0 );
    if(rc != SQLITE_OK) {
	printf("ERROR prep rc: %d\n", rc);
	return 1;
    } 
    
    rc = sqlite3_bind_blob( stmt, 1, (const void*) data, len, SQLITE_STATIC);
    if(rc != SQLITE_OK) {
	printf("ERROR: bind BSON 1 rc: %d\n", rc);
	return 1;
    }
    rc = sqlite3_bind_blob( stmt, 2, (const void*) data2, len2, SQLITE_STATIC);
    if(rc != SQLITE_OK) {
	printf("ERROR: bind BSON 2 rc: %d\n", rc);
	return 1;
    }    

    // Doing an insert means we expect a single result DONE back:
    rc = sqlite3_step( stmt );
    if(rc != SQLITE_DONE) {
	printf("? INSERT yields rc %d\n", rc);
    }
	
    rc = sqlite3_clear_bindings( stmt );
    rc = sqlite3_reset( stmt );
    rc = sqlite3_finalize( stmt );  //  Finalize the prepared stat

    return 0; // OK
}

static int create(sqlite3 *db) {
    sqlite3_stmt* stmt = 0;

    // Note we can call the column type something other than the other
    // official sqlite types; it doesn't matter.
    char* sql = "create table if NOT EXISTS bsontest (bdata BSON, bdata2 BSON)";    

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0 );
    if(rc != SQLITE_OK) {
	printf("ERROR prep rc: %d\n", rc);
	return 1;
    } 
    
    // Doing an insert means we expect a single result DONE back:
    rc = sqlite3_step( stmt );
    if(rc != SQLITE_DONE) {
	printf("? CREATE yields rc %d\n", rc);
	return 1;
    }
    rc = sqlite3_clear_bindings( stmt );
    rc = sqlite3_reset( stmt );
    rc = sqlite3_finalize( stmt );  //  Finalize the prepared stat

    return 0; // OK
}



int activate_extension(sqlite3 *db)
{
    const char* ext_path = "bsonext"; // prob should be a cmdline option...
    const char* entry_point = "sqlite3_bson_init"; // ALWAYS the same!

    int rc = sqlite3_enable_load_extension(db, 1); // TRUE
    if(rc != SQLITE_OK) {
	printf("error: cannot enable extension loading");
	return 1;
    }
    
    char *zErrMsg = 0;
    rc = sqlite3_load_extension(db, ext_path, entry_point, &zErrMsg);
    if(rc != SQLITE_OK) {
	printf("error: load ext [%s] failed: %d: %s\n", ext_path, rc, zErrMsg);
	sqlite3_free(zErrMsg);
	return 1;
    }

    return 0; // OK
}

struct scalar_test {
    const char* name;
    char* (*f)(sqlite3*,const char*,bson_type_t,const void*);
    const char* a1;
    bson_type_t a2;
    const void* a3;
};

struct changes_test {
    const char* name;
    char* (*f)(sqlite3*,const char*,int exp_changes);
    const char* a1; // sql
    int a2; // exp changes
};


#define DO_TEST(f) (

/*
  usage:  test1 [ someFile.sqlite3 ]
 */
int main(int argc, char* argv[]) {
    sqlite3 *db;

    int rc;

    char* dbf = argv[1];
   
    rc = sqlite3_open(dbf, &db);

    if( rc ) {
	fprintf(stderr, "cannot open [%s]: %s\n", dbf, sqlite3_errmsg(db));
	return(0);
    } else {
	fprintf(stderr, "Opened [%s] successfully\n", dbf);
    }

    if(0 != activate_extension(db)) { return 1; }
    if(0 != create(db)) { return 1; }
    if(0 != insert(db)) { return 1; }

    //basic_scalar_test(db, "select bson_get(bdata,'hdr.id') from bsontest", BSON_TYPE_UTF8, "A0");    

    int zval = 0;
    int oval = 1;        
    
    double dval = 3.14159;
    int ival = 7;
    long lval = 743859238573L;

    const char* fake_binary = "Pretend this is a JPEG";
    char bval[128];
    int n, idx = 0;
    for(n = 0; n < strlen(fake_binary); n++) {
	// Love that pointer math....
	sprintf(bval+idx, "%02x", (uint8_t)fake_binary[n]);
	idx += 2;
    }
    bval[idx] = '\0';


    struct scalar_test XXX[] = {
	{"string exists", basic_scalar_test, "select bson_get(bdata,'hdr.id') from bsontest", BSON_TYPE_UTF8, "A0"},

	{"field !exists", basic_scalar_test, "select bson_get(bdata,'not.here') from bsontest", BSON_TYPE_NULL, 0},

	{"no row at all", basic_scalar_test, "select bson_get(bdata,'hdr.id') from bsontest where FALSE", BSON_TYPE_EOD, 0},

	
	{"double exists", basic_scalar_test, "select bson_get(bdata,'A.B.2') from bsontest", BSON_TYPE_DOUBLE, &dval},	// 3.14159

	{"int32 exists", basic_scalar_test, "select bson_get(bdata,'A.B.0') from bsontest", BSON_TYPE_INT32, &ival},

	{"int64 exists", basic_scalar_test, "select bson_get(bdata,'hdr.bigint') from bsontest", BSON_TYPE_INT64, &lval},


	// decimal, dates, and binary have no type equiv in sqlite; they
	// emerge as strings:
	{"date exists", basic_scalar_test, "select bson_get(bdata,'hdr.ts') from bsontest", BSON_TYPE_UTF8, "2023-01-12T13:14:15.678Z"},
	{"decimal exists", basic_scalar_test, "select bson_get(bdata,'amt') from bsontest", BSON_TYPE_UTF8, "10.09"},
	{"binary exists", basic_scalar_test, "select bson_get(bdata,'thumbnail') from bsontest", BSON_TYPE_UTF8, &bval},
    };

    for(int q = 0; q < sizeof(XXX)/sizeof(struct scalar_test); q++) {
	exec_bst(db,XXX[q].name, XXX[q].a1, XXX[q].a2, XXX[q].a3);
    }

    // Recall jbuf and jbuf2 differ by a bit!
    exec_bst(db,"verify bdata = bdata2 is false", "select bdata = bdata2 from bsontest", BSON_TYPE_INT32, &zval);
    
    exec_bct(db,"internal BSON copy", "update bsontest set bdata2 = bdata", 1);
    exec_bst(db,"internal BSON copy verify", "select bdata = bdata2 from bsontest", BSON_TYPE_INT32, &oval);

    // Note this changes the column from type BLOB to type integer which
    // should be caught by the "if not BLOB" logic...
    exec_bct(db,"break bdata2 on purpose", "update bsontest set bdata2 = 17", 1);
    exec_bst(db,"verify broken bdata2", "select bdata = bdata2 from bsontest", BSON_TYPE_INT32, &zval);

    ival = 20;
    exec_bst(db,"check int ops broken bdata2", "select 3 + bdata2 from bsontest", BSON_TYPE_INT32, &ival);
    
    
    sqlite3_close(db);
}





