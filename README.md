sqlitebson
==========

BSON support for sqlite

Introduction
============

This sqlite extension realizes the BSON data type, together with functions to create and inspect BSON objects for the purposes of expressive and performant
querying.

BSON (http://bsonspec.org/) is a high-performance, richly-typed data carrier
similar to JSON but offers a number of attractive features including:

 *  Datetimes, decimal (numeric), and byte[] are first class types.  In pure
    JSON these must all be represented as a string, requiring conversion,
    potentially introducing lossiness, and impairing native operations
    like `>` and `<=`.
 *  Performance.  Moving binary BSON in and out of the database under some
    conditions is almost 10x faster than using native `jsonb` or `json` because
    it avoids to- and from-string and to-dictionary conversion.
 *  Roundtrip ability.  BSON is binary spec, not a string.  There is no whitespace,
    quoting rules, etc.  BSON that goes into Postgres comes out *exactly* the
    same way, each time, every time.
 *  Standard SDK implementations in upwards of 20 languages


The extension expects binary BSON to be stored in a BLOB type column.

The extension has four basic types of accessors that take a BSON (BLOB)
column and a dotpath to descend into the structure:
*  Typesafe high performance accessor functions that take a dotpath notation
    to get to a field e.g.<br>
    ```
    select bson_get_datetime(bson_column, 'msg.header.event.ts') from table;
    select bson_get_string(bson_column, 'msg.header.event.type') from table;    
    ```

*  Generic fetching where the return type is as appropriate as possible.
   sqlite is very good at letting return values be polymorphic and then
   interpreting their subsequent use.
    ```
    select bson_get(bson_column, 'data.someInt32') ... return int
    select bson_get(bson_column, 'data.someInt64') ... returns int64
    select bson_get(bson_column, 'data.someDouble') ... returns double
    select bson_get(bson_column, 'data.someDecimal128') ... returns string to avoid floating point conversion issues
    select bson_get(bson_column, 'data.someDate') ... returns string
    ```
    If the target item is not a scalar (i.e. a substructure or array) then
    the JSON equivalent is returned as a string.

*  Native BSON substructures e.g. <br>
    ```
    select bson_get_bson(bson_column, 'msg.header') ...
    ```
    This will return native binary BSON which by itself is not very useful
    in the CLI but in actual programs, the BSON can be easily manipulated by the
    BSON SDK.

*  To JSON e.g. <br>
   ```
   select bson_as_json(bson_column) ... returns string
   ```
   Combined with both `bson_get_bson()` and the native sqlite JSON functions
   e.g. `json_extract`, this provides a means to very performantly "dig"
   into deep structures without parsing through lots of JSON that you do not
   need.  For example, consider this BSON structure (shown in simplified JSON):
   ```
   data: {
     hdr: {type: "X", ts: "2023-01-01T12:13:14Z", subtype: "A"},
     payload: {
       id: "ABC123",
       region: "EAST",
       ( 4K of other fields and data)
     }
   }
   ```
   Suppose we wish to find `hdr` of type `X`.  Converting the `data` to JSON
   means converting the *entire* structure including 4K of other fields that
   we don't need to process; instead, we do this:
   ```
   select bson_get_string(bson_column, "data.payload.id") from table
      where bson_get_string(bson_column, "data.hdr.type") = 'X';
   ```
   In the extension, the BSON C SDK is being called with the dotpath to
   performantly descend into the structure.  The 4K of other fields are
   never converted to JSON yielding significant performance improvement. 
   		    

Status
======

Experimental.  All contribs / PRs / comments / issues welcome.


Example
=======
Dealing with binary BSON through the CLI is not very interesting
so let's make a little program to insert some data.

    Note that sqlite doesn't really care about the column type name!
    sqlite> create table MYDATA ( bdata BSON );

    $ cat test1.c
    
    #include <stdio.h>
    #include <sqlite3.h>

    #include <bson.h> // The BSON C SDK headers

    static void insert(sqlite3 *db, int nn) {
        sqlite3_stmt* stmt = 0;

        char jbuf[256];

        // Use the handy bson_new_from_json() to parse some JSON.  Note the
        // JSON is Extended JSON (EJSON).  The BSON SDK recognizes the special keys
        // like $date and $numberDecimal and will construct the appropriate
        // BSON type; remember, BSON has types not natively support by JSON.
        // EJSON always takes a string as the "argument" to the special keys
        // to prevent incorrect parsing of the value -- esp. important for
        // floating point and penny-precise data!
        // Note we create a incrementing hdr.id here with the input nn:
        sprintf(jbuf, "{\"hdr\":{\"id\":\"A%d\", \"ts\":{\"$date\":\"2023-01-12T13:14:15.678Z\"}}, \"amt\":{\"$numberDecimal\":\"10.09\"},  \"A\":{\"B\":[ 7 ,{\"X\":\"QQ\", \"Y\":[\"ee\",\"ff\"]}    ]} }", nn);

    	bson_error_t err; // on stack
    	bson_t* b = bson_new_from_json((const uint8_t *)jbuf, strlen(jbuf), &err);

    	const uint8_t* data = bson_get_data(b);
        int32_t len = b->len;

	// Normally you should check the return value for success for each
	// of these calls but for clarity we omit them here:
	sqlite3_prepare_v2(db, "INSERT INTO MYDATA (bdata) values (?)", -1, &stmt, 0 );
	// 
	sqlite3_bind_blob( stmt, 1, (const void*) data, len, SQLITE_STATIC);
	sqlite3_step( stmt ); // Doing an insert means we expect a single result DONE back:
	sqlite3_finalize( stmt );  //  Finalize the prepared stat
    }

    int main(int argc, char* argv[]) {
        sqlite3 *db;
        char *zErrMsg = 0;
        int rc;

    	char* dbf = argv[1];
   
	rc = sqlite3_open(dbf, &db);
	if( rc != SQLITE_OK ) {
	    fprintf(stderr, "cannot open [%s]: %s\n", dbf, sqlite3_errmsg(db));
	    return(0);
    	}

	for(int i = 0; i < 10; i++) {
	    insert(db, i);
	}
    
        sqlite3_close(db);
    }

Then back in the sqlite CLI:

    sqlite will try various suffixes (e.g. .dylib or .so) and prefixes to 
    load the extension.  Regardless of where you put the library, the
    entry point is always the same:  sqlite3_bson_init
    sqlite> .load path/to/bsonext sqlite3_bson_init

    sqlite> select bson_as_json(bdata) from MYDATA where bson_get_string(bdata, "hdr.id") = 'A34';
    { "hdr" : { "id" : "A34", "ts" : { "$date" : "2023-01-12T13:14:15.678Z" } }, "amt" : { "$numberDecimal" : "10.09" }, "A" : { "B" : [ 7, { "X" : "QQ", "Y" : [ "ee", "ff" ] } ] } }

    sqlite> select bson_get_decimal128(bdata, "amt") / 2 from foo where bson_get_string(bdata, "hdr.id"
    5.045



Building
========

Tested using sqlite 3.40.1 2022-12-28 on OS X 10.15.7

Requires:

 *  sqlite development SDK (`sqlite3.h` and `sqlite3ext.h`).  These are part
    of the Source Code distribution nominally available
    <a href="https://www.sqlite.org/2022/sqlite-amalgamation-3400100.zip">HERE</a>

 *  `libbson.so` and BSON C SDK `.h` files.  You can make these separately and
    there is plenty of material on this topic.
    
 *  C compiler.  No C++ used. 

Your compile/link environment should look something like this:
    $ gcc -fPIC -dynamiclib -I/path/to/bson/include -I/path/to/sqlite/sdk -Lbson/lib -lbson -lsqlite3  bsonext.c -o bsonext.dylib

Issues with OS X
----------------
OS X 10.15 and likely other versions comes with libsqlite.dylib pre installed
with the OS:
    $ ls -l /usr/lib/libsqlite3.dylib 
    -rwxr-xr-x  1 root  wheel  4344864 Oct 30  2020 /usr/lib/libsqlite3.dylib
Unfortuntely, that install is 2 years out of date and does not have either
the built-in JSON functions nor the `sqlite_load_extension` API.  The newer
sources do, of course, but the problem is `/usr/lib` is off-limits in OS X 10.15+; even with `sudo` you cannot copy in a newly compiled version and switch the
symlinks.  The problem this creates is that the linker always searches `/usr/lib` first and will thus find the OLD version of the lib.  The `-L` argument to
the linker does not help because that *appends* a path to search, not *prepend*.To get around this problem it is necessary to disable the default library search path with the `-Z` option and rebuild the path from scratch, something like this:
```
gcc -fPIC -dynamiclib -I/path/to/bson/include -I/path/to/sqlite/sdk -Z -Lbson/lib -lbson.1 -Lcodes/sqlite-amalgamation-3400100 -lsqlite3 -L/usr/lib  bsonext.c -o bsonext.dylib
```






    


