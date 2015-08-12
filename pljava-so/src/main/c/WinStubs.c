#include <postgres.h>
#include <pgtime.h>

/*
 * Postgresql 9.4 does not export all symbols on Windows.
 * This stub file provides minimal default values to satisfy the linker,
 * albeit with potentially reduced functionality.
 */
#ifdef _MSC_VER 
#if (PGSQL_MAJOR_VER > 9 || (PGSQL_MAJOR_VER == 9 && PGSQL_MINOR_VER >= 4))
// from src/timezone/tzfile.h
#define TZ_MAX_TIMES	1200

#define TZ_MAX_TYPES	256		/* Limited by what (unsigned char)'s can hold */

#define TZ_MAX_CHARS	50		/* Maximum number of abbreviation characters */
 /* (limited by what unsigned chars can hold) */

#define TZ_MAX_LEAPS	50		/* Maximum number of leap second corrections */

// from src/timezone/pgtz.h
#define BIGGEST(a, b)	(((a) > (b)) ? (a) : (b))

struct ttinfo
{								/* time type information */
	long		tt_gmtoff;		/* UTC offset in seconds */
	int			tt_isdst;		/* used to set tm_isdst */
	int			tt_abbrind;		/* abbreviation list index */
	int			tt_ttisstd;		/* TRUE if transition is std time */
	int			tt_ttisgmt;		/* TRUE if transition is UTC */
};

struct lsinfo
{								/* leap second information */
	pg_time_t	ls_trans;		/* transition time */
	long		ls_corr;		/* correction to apply */
};

struct state
{
	int			leapcnt;
	int			timecnt;
	int			typecnt;
	int			charcnt;
	int			goback;
	int			goahead;
	pg_time_t	ats[TZ_MAX_TIMES];
	unsigned char types[TZ_MAX_TIMES];
	struct ttinfo ttis[TZ_MAX_TYPES];
	char		chars[BIGGEST(BIGGEST(TZ_MAX_CHARS + 1, 3 /* sizeof gmt */ ),
										  (2 * (TZ_STRLEN_MAX + 1)))];
	struct lsinfo lsis[TZ_MAX_LEAPS];
};

struct pg_tz
{
	/* TZname contains the canonically-cased name of the timezone */
	char		TZname[TZ_STRLEN_MAX + 1];
	struct state state;
};

// Dummy stubs for Windows link of Postgresql 9.4+
int			log_min_messages = WARNING;
int			client_min_messages = NOTICE;
pg_tz	   *session_timezone = NULL;	// pg_tzset("GMT");
#endif
#endif
