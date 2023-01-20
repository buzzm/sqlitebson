sqlitebson
==========

BSON support for sqlite

Introduction
============

This sqlite extension provides accessor functions to the BSON data type for the
purposes of expressive and performant querying.

TL;DR: You will need BSON and sqlite development C language SDKs (header files and dynamic/shared libraries); then:
```
    sqlite> .load bsonext sqlite3_bson_init
    sqlite> select bson_get(bson_column, "path.to.some.string_field") from table;
    hello world
    sqlite> select bson_get(bson_column, "path.to.some.num_field") from table;
    29
    sqlite> select bson_get(bson_column, "path.to.some") from table;
    {"string_field": "hello world", "num_field": 29}
    sqlite> select bson_get_bson(bson_column, "path.to.some") from table;
    (returned as native BSON)

    sqlite> create index IDX1 on table ( bson_get(bdata,"hdr.id") );
    sqlite> explain query plan select * from foo where bson_get(bdata, "hdr.id") = 'A2';
    QUERY PLAN
    `--SEARCH foo USING INDEX IDX1 (<expr>=?)

    Use views to expose some basic scalar columns to simplify some queries:
    
    sqlite> create view easy_table as
    ...> select bson_get(bdata, "hdr.id") as ID, bson_get(bdata, "A.B.0") as code, bdata from table;
    sqlite> select code, bson_get(bdata,"") from easy_table where ID = 'A2';
    7|{ "hdr" : { "id" : "A2", "ts" : { "$date" : "2023-01-12T13:14:15.678Z" } }, "amt" : { "$numberDecimal" : "10.09" }, "A" : { "B" : [ 7, { "X" : "QQ", "Y" : [ "ee", "ff" ] }, 3.1415899999999998826 ] } }
    
```    

BSON (http://bsonspec.org) is a high-performance, richly-typed data carrier
similar to JSON but offers a number of attractive features including:

 *  Datetimes, decimal (numeric), and byte[] are first class types.  In pure
    JSON these must all be represented as a string, requiring conversion,
    potentially introducing lossiness, and impairing native operations
    like `>` and `<=`.
 *  Roundtrip ability.  BSON is binary spec, not a string.  There is no whitespace,
    quoting rules, etc.  BSON that goes into sqlite comes out *exactly* the
    same way, each time, every time.
 *  Standard SDK implementations in upwards of 20 languages


The extension expects binary BSON to be stored in a BLOB type column.


The extension has 2 accessors that take a BSON (BLOB)
column and a dotpath to descend into the structure:

*  Generic fetching where the return type is as appropriate as possible.
   sqlite is very good at letting return values be polymorphic and then
   interpreting their subsequent use.
    ```
    select bson_get(bdata, 'data.someInt32') ... return int
    select bson_get(bdata, 'data.someInt64') ... returns int64
    select bson_get(bdata, 'data.someDouble') ... returns double
    select bson_get(bdata, 'data.someDecimal128') ... returns string to avoid floating point conversion issues
    select bson_get(bdata, 'data.someDate') ... returns string
    ```
    If the target item is not a scalar (i.e. a substructure or array) then
    the JSON equivalent is returned as a string.  If a "blank" dotpath is
    supplied then no descent is made and the whole BSON object is converted
    to JSON:
    ```
    select bson_get(bdata,"") ... returns JSON string of complete BSON object
    ```
    
    Only the target
    is converted to JSON, not any other fields, which improves performance.
    For example, given a structure like this:
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
   To print just the header structure we can do this:
   ```
   sqlite> select bson_get(bdata,"data.hdr") from MYDATA ...
   { hdr: {type: "X", ts: "2023-01-01T12:13:14Z", subtype: "A"} }
   ```    
   Only a fraction of the data will be converted to JSON.

   Similarly, suppose we wish to capture the `id` for those documents
   where `hdr` is of type `X`.  Converting the `data` to JSON
   means converting the *entire* structure including 4K of other fields that
   we don't need to process; instead, we do this:
   ```
   select bson_get(bdata, "data.payload.id") from table
      where bson_get(bdata, "data.hdr.type") = 'X';
   ```
   In the extension, the BSON C SDK is being called with the dotpath to
   performantly descend into the structure.  The 4K of other fields are
   never converted to JSON yielding significant performance improvement. 

   `bson_get()` is a deterministic and innocuous function and therefore
   is suitable for use in functional indexes e.g.
   ```
   create index XX on MYDATA (bson_get(bdata, "data.payload.id"));
   ```



*  Native BSON substructures e.g. <br>
    ```
    select bson_get_bson(bdata, 'data.payload') ...
    ```
    This will return the whole payload substructure in native binary BSON.
    Binary data is not particularly useful in the CLI but in actual sqlite
    client-side programs, the row-column returned by `sqlite_step()` and then
    `sqlite3_column_blob()` is easily initialized into a BSON object and 
    manipulated with the BSON SDK.  This allows client side programs to avoid
    potentially lossy to- and from- JSON conversion e.g. floating point of
    6.0 gets emitted as 6 then reparsed as an int.  See Example below.

    Note there is no purpose in calling `bson_get_bson()` with a blank dotpath;
    this is equivalent to simply returning the raw BLOB:
    ```
    select bdata from  ... returns native binary BSON of complete BSON object
    ```
    
    
    


Status
======

Experimental.  All contribs / PRs / comments / issues welcome.


Example
=======
Try compiling and linking `example1.c`, included in this repo.  A prototype
Makefile highlighting the workarounds for OS X `/usr/lib/libsqlite.dynlib`
issue should get you going.  Linux and other targets pretty much use the
same simple compile/link line except with `.so` instead of `.dynlib`, etc.


Dealing with binary BSON through the CLI is not very interesting
so let's make a little program to insert some data.

    Note that sqlite doesn't really care about the column type name!
    sqlite> create table MYDATA ( bdata BSON );

    #include <stdio.h>
    #include <sqlite3.h>

    #include <bson.h> // The BSON C SDK headers

    static void insert(sqlite3 *db, int nn) {
        sqlite3_stmt* stmt = 0;

        char jbuf[256];

        // Use the handy bson_new_from_json() to parse some JSON.  We could
	// use the low-level C API (bson_append_int, bson_append_utf8, etc.)
	// but that is tedious for this example.  Note the
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

    sqlite> select bson_get(bdata,"") from MYDATA where bson_get(bdata, "hdr.id") = 'A34';
    { "hdr" : { "id" : "A34", "ts" : { "$date" : "2023-01-12T13:14:15.678Z" } }, "amt" : { "$numberDecimal" : "10.09" }, "A" : { "B" : [ 7, { "X" : "QQ", "Y" : [ "ee", "ff" ] } ] } }

    sqlite> select bson_get(bdata, "amt") / 2 from foo where bson_get(bdata, "hdr.id") = 'A12';
    5.045


Querying in a client-side program:

    int rc = sqlite3_prepare_v2(db, "SELECT bdata from MYDATA WHERE bson_get(bdata, \"hdr.id\") = ?", -1, &stmt, 0 );    

    rc = sqlite3_bind_text( stmt, 1, "A34", -1, SQLITE_STATIC);

    while ( sqlite3_step( stmt ) == SQLITE_ROW ) {
	if(sqlite3_column_type(stmt,0) != SQLITE_NULL) {
	    const void* data = sqlite3_column_blob(stmt, 0);
	    int len = sqlite3_column_bytes(stmt, 0);

	    bson_t b;
	    bson_init_static(&b, data, len);

	    // At this point, you have a complete BSON object that can be
	    // interogated and visited, etc.  Just for show, let's turn it into
	    // JSON and print:
	    size_t jsz;
	    printf("%s\n", bson_as_canonical_extended_json (&b, &jsz));
        }
    }
    

SQLite, BSON, Swift, and the iPhone
-----------------------------------
iPhone app development in iOS/Swift/Xcode is beyond the scope of this
extension; however, experiments show that the SQLite.swift package
in combination with the
[official pure Swift BSON implementation](https://github.com/mongodb/swift-bson)
can create BSON objects, convert them to `Data` using `BSON.toData()`,
insert them as SQLite BLOBS, then fetch and decoded them back into BSON
objects with `var doc: BSONDocument = try BSONDocument(fromBSON: data)`


TBD:  Need a robust way to add these BSON extension functions into the
Xcode/iOS build process.


Building
========

Tested using sqlite 3.40.1 2022-12-28 on OS X 10.15.7

Requires:

 *  sqlite development SDK (`sqlite3.h` and `sqlite3ext.h`).  These are part
    of the Source Code distribution nominally available
    [HERE](https://www.sqlite.org/2022/sqlite-amalgamation-3400100.zip)

 *  `libbson.so` and BSON C SDK `.h` files.  You can make these separately and
    there is plenty of material on this topic but
    [here is a start](https://github.com/mongodb/mongo-c-driver/tree/master/src/libbson).  MongoDB actively ensures the C implementation of BSON is
    highly portable and compilable for many targets.
    
 *  C compiler.  No C++ used. 


Your compile/link environment should look something like this:
```
$ gcc -fPIC -dynamiclib -I/path/to/bson/include -I/path/to/sqlite/sdk -Lbson/lib -lbson -lsqlite3  bsonext.c -o bsonext.dylib
```



Issues with pre-installed libsqlite
-----------------------------------
OS X 10.15 and likely other platforms come with libsqlite.dylib (or .so)
pre-installed with the OS:

    $ ls -l /usr/lib/libsqlite3.dylib 
    -rwxr-xr-x  1 root  wheel  4344864 Oct 30  2020 /usr/lib/libsqlite3.dylib

Unfortuntely, that install is 2 years out of date and does not have either
the built-in JSON functions nor the `sqlite_load_extension` API.  The newer
sources do, of course, but the problem is `/usr/lib` is off-limits in OS X 10.15+; even with `sudo` you cannot copy in a newly compiled version and switch the
symlinks.  The problem this creates is that the linker always searches `/usr/lib` first and will thus find the OLD version of the lib.  The `-L` argument to
the linker does not help because that *appends* a path to search, not *prepend*.
  To get around this problem it is necessary to disable the default library search path with the `-Z` option and rebuild the path from scratch, something like this:
```
gcc -fPIC -dynamiclib -I/path/to/bson/include -I/path/to/sqlite/sdk -Z -Lbson/lib -lbson.1 -Lcodes/sqlite-amalgamation-3400100 -lsqlite3 -L/usr/lib  bsonext.c -o bsonext.dylib
```






    


