
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
  rc = sqlite3_create_function(db, "rot13", 1,
                   SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                   0, rot13func, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_collation(db, "rot13", SQLITE_UTF8, 0, rot13CollFunc);
  }
  return rc;
}

