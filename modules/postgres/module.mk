SOURCES="postgres.c"
MODULES="postgres"
LDFLAGS="-lpq"

check_postgres()
{
    $APP_C $APP_CFLAGS $1 -x c -o /dev/null -c $MODULE/postgres.c >/dev/null 2>&1
}

postgres_configure()
{
    if ! check_postgres ; then
            return 1
    fi
}

if ! postgres_configure ; then
    ERROR="PostgreSQL or libpq not found"
fi
