static int psyco_cast_types_NUMBER[] = {20, 23, 21, 701, 700, 1700, 0};
static int psyco_cast_types_LONGINTEGER[] = {20, 0};
static int psyco_cast_types_INTEGER[] = {23, 21, 0};
static int psyco_cast_types_FLOAT[] = {701, 700, 1700, 0};
static int psyco_cast_types_STRING[] = {19, 18, 25, 1042, 1043, 0};
static int psyco_cast_types_BOOLEAN[] = {16, 0};
static int psyco_cast_types_DATETIME[] = {1082, 1083, 1266, 1114, 1184, 704, 1186, 0};
static int psyco_cast_types_TIME[] = {1083, 1266, 0};
static int psyco_cast_types_DATE[] = {1082, 1114, 1184, 0};
static int psyco_cast_types_INTERVAL[] = {704, 1186, 0};
static int psyco_cast_types_BINARY[] = {17, 0};
static int psyco_cast_types_ROWID[] = {26, 0};


psyco_DBAPIInitList psyco_cast_types[] = {
    {"NUMBER", psyco_cast_types_NUMBER, psyco_NUMBER_cast},
    {"LONGINTEGER", psyco_cast_types_LONGINTEGER, psyco_LONGINTEGER_cast},
    {"INTEGER", psyco_cast_types_INTEGER, psyco_INTEGER_cast},
    {"FLOAT", psyco_cast_types_FLOAT, psyco_FLOAT_cast},
    {"STRING", psyco_cast_types_STRING, psyco_STRING_cast},
    {"BOOLEAN", psyco_cast_types_BOOLEAN, psyco_BOOLEAN_cast},
    {"DATETIME", psyco_cast_types_DATETIME, psyco_DATETIME_cast},
    {"TIME", psyco_cast_types_TIME, psyco_TIME_cast},
    {"DATE", psyco_cast_types_DATE, psyco_DATE_cast},
    {"INTERVAL", psyco_cast_types_INTERVAL, psyco_INTERVAL_cast},
    {"BINARY", psyco_cast_types_BINARY, psyco_BINARY_cast},
    {"ROWID", psyco_cast_types_ROWID, psyco_ROWID_cast},
    {NULL, NULL, NULL}
};

