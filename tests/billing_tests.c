#include "common.h"
#include "vnstat_tests.h"
#include "billing_tests.h"
#include "cfg.h"
#include "dbsql.h"
#include "dbshow.h"
#include "misc.h"
#include "datacache.h"
#include "daemon.h"
#include "ifinfo.h"
#include "percentile.h"

static void billing_fixture(void)
{
	setup();
	setenv("TZ", "UTC", 1);
	tzset();
	clean_testdbdir();
	strcpy(cfg.dbdir, TESTDBDIR);
}

START_TEST(billing_config_validation)
{
	int day, minute;

	for (day = 1; day <= 31; day++) {
		cfg.monthrotate = day;
		validatecfg(CT_All);
		ck_assert_int_eq(cfg.monthrotate, day);
	}
	for (minute = 0; minute < 60; minute++) {
		cfg.monthrotatehour = 23;
		cfg.monthrotateminute = minute;
		validatecfg(CT_All);
		ck_assert_int_eq(cfg.monthrotatehour, 23);
		ck_assert_int_eq(cfg.monthrotateminute, minute);
	}
	disable_logprints();
	cfg.monthrotate = 0;
	cfg.monthrotatehour = 24;
	cfg.monthrotateminute = -1;
	validatecfg(CT_All);
	ck_assert_int_eq(cfg.monthrotate, MONTHROTATE);
	ck_assert_int_eq(cfg.monthrotatehour, 0);
	ck_assert_int_eq(cfg.monthrotateminute, 0);
	cfg.monthrotate = 32;
	cfg.monthrotatehour = -1;
	cfg.monthrotateminute = 60;
	validatecfg(CT_All);
	ck_assert_int_eq(cfg.monthrotate, MONTHROTATE);
	ck_assert_int_eq(cfg.monthrotatehour, 0);
	ck_assert_int_eq(cfg.monthrotateminute, 0);
}
END_TEST

static const char *monthend_zones[] = {"UTC", "Asia/Singapore", "America/New_York", "Europe/Berlin"};
static const struct {
	int year, month, days, nextdays;
} monthend_cases[] = {
	{2023, 1, 31, 28}, {2023, 2, 28, 31}, {2023, 3, 31, 30}, {2023, 4, 30, 31},
	{2023, 12, 31, 31}, {2024, 1, 31, 29}, {2024, 2, 29, 31}, {2024, 3, 31, 30},
	{2024, 10, 31, 30}, {2024, 12, 31, 31}, {2000, 2, 29, 31}, {2100, 2, 28, 31}
};
static const int monthend_clocks[][2] = {{0, 0}, {18, 24}, {23, 59}};

static void monthend_timezone(const int variant)
{
	setenv("TZ", monthend_zones[variant / 2], 1);
	tzset();
	cfg.useutc = variant % 2;
	validatecfg(CT_All);
}

static time_t monthend_label(const int year, const int month)
{
	struct tm calendar = {0};

	calendar.tm_year = year - 1900;
	calendar.tm_mon = month - 1;
	calendar.tm_mday = 1;
	calendar.tm_isdst = -1;
	return mktime(&calendar);
}

static time_t monthend_querytime(const time_t timestamp)
{
	struct tm calendar;

	if (!cfg.useutc) {
		return timestamp;
	}
	calendar = *gmtime(&timestamp);
	calendar.tm_isdst = -1;
	return mktime(&calendar);
}

START_TEST(billing_monthend_periods)
{
	time_t label, start, end;
	dbdatalist *data = NULL;
	uint64_t rx, tx;
	int c, day, clock, year, month;

	monthend_timezone(_i);
	for (c = 0; c < (int)(sizeof(monthend_cases) / sizeof(monthend_cases[0])); c++) {
		year = monthend_cases[c].year;
		month = monthend_cases[c].month;
		label = monthend_label(year, month);
		for (day = 1; day <= 31; day++) {
			cfg.monthrotate = day;
			for (clock = 0; clock < 3; clock++) {
				cfg.monthrotatehour = monthend_clocks[clock][0];
				cfg.monthrotateminute = monthend_clocks[clock][1];
				start = (time_t)get_timestamp(year, month, day < monthend_cases[c].days ? day : monthend_cases[c].days,
				                             cfg.monthrotatehour, cfg.monthrotateminute);
				end = (time_t)get_timestamp(year + (month == 12), month == 12 ? 1 : month + 1,
				                           day < monthend_cases[c].nextdays ? day : monthend_cases[c].nextdays,
				                           cfg.monthrotatehour, cfg.monthrotateminute);
				ck_assert_int_eq(billingperiodstart(label, 0, 0), start);
				ck_assert_int_eq(billingperiodstart(label, 0, 1), end);
				ck_assert_int_eq(billingperiodstart(monthend_label(year + (month == 12), month == 12 ? 1 : month + 1), 0, -1), start);
				ck_assert_int_eq(issametimeslot(LT_Month, label, monthend_querytime(start - 60)), 0);
				ck_assert_int_eq(issametimeslot(LT_Month, label, monthend_querytime(start)), 1);
				ck_assert_int_eq(issametimeslot(LT_Month, label, monthend_querytime(end - 60)), 1);
				ck_assert_int_eq(issametimeslot(LT_Month, label, monthend_querytime(end)), 0);
				ck_assert_int_eq(getperiodseconds(LT_Month, label, 0, 0, 0), end - start);
				ck_assert_int_eq(mosecs(label, monthend_querytime(start + 300)), 300);
				ck_assert_int_eq(dbdatalistadd(&data, 1000, 2000, label, 1), 1);
				getestimates(&rx, &tx, LT_Month, monthend_querytime(start + 1000), 0, &data);
				ck_assert_int_eq(rx, end - start);
				ck_assert_int_eq(tx, 2 * (end - start));
				dbdatalistfree(&data);
			}
		}
	}
}
END_TEST

/* 对 1..28 日保留上游原本的 SQL 表达式，连同 SQLite 在远期年份里受平台影响的 localtime 映射一起对比 */
static void monthend_legacy_sql_label(char label[11], const time_t timestamp)
{
	sqlite3_stmt *statement;
	char sql[512];
	const char *value;

	snprintf(sql, sizeof(sql),
	         "select strftime('%%Y-%%m-01', datetime(datetime(%" PRIu64 ", 'unixepoch')%s, '-%d days', '-%d hours', '-%d minutes'))",
	         (uint64_t)timestamp, cfg.dbtzmodifier, cfg.monthrotate - 1,
	         cfg.monthrotatehour, cfg.monthrotateminute);
	ck_assert_int_eq(sqlite3_prepare_v2(db, sql, -1, &statement, NULL), SQLITE_OK);
	ck_assert_int_eq(sqlite3_step(statement), SQLITE_ROW);
	value = (const char *)sqlite3_column_text(statement, 0);
	ck_assert_ptr_ne(value, NULL);
	strncpy_nt(label, value, 11);
	ck_assert_int_eq(sqlite3_finalize(statement), SQLITE_OK);
}

START_TEST(billing_monthend_database_labels)
{
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	time_t boundary;
	const int offsets[] = {-60, -1, 0, 1, 60};
	const uint64_t sample_rx[] = {10, 1, 30, 3, 50};
	const uint64_t sample_tx[] = {20, 2, 40, 4, 60};
	struct {
		char label[11];
		uint64_t rx, tx;
	} legacy[5];
	int day, clock, k, nlegacy = 0, c = _i / 8;
	int year = monthend_cases[c].year, month = monthend_cases[c].month;

	monthend_timezone(_i % 8);
	ck_assert_int_eq(db_open_rw(1), 1);
	for (day = 1; day <= 31; day++) {
		cfg.monthrotate = day;
		/* 1..28 日零点场景由专门用例和原版 SQL 直接核对 */
		for (clock = day <= 28 ? 1 : 0; clock < 3; clock++) {
			cfg.monthrotatehour = monthend_clocks[clock][0];
			cfg.monthrotateminute = monthend_clocks[clock][1];
			boundary = (time_t)get_timestamp(year, month, day < monthend_cases[c].days ? day : monthend_cases[c].days,
			                                cfg.monthrotatehour, cfg.monthrotateminute);
			ck_assert_int_eq(db_addinterface("monthend0"), 1);
			ck_assert_int_eq(db_addtraffic_dated("monthend0", 10, 20, (uint64_t)(boundary - 60)), 1);
			ck_assert_int_eq(db_addtraffic_dated("monthend0", 1, 2, (uint64_t)(boundary - 1)), 1);
			ck_assert_int_eq(db_addtraffic_dated("monthend0", 30, 40, (uint64_t)boundary), 1);
			ck_assert_int_eq(db_addtraffic_dated("monthend0", 3, 4, (uint64_t)(boundary + 1)), 1);
			ck_assert_int_eq(db_addtraffic_dated("monthend0", 50, 60, (uint64_t)(boundary + 60)), 1);
			if (day <= 28) {
				/* 逐条对照原 SQL 输出而不是理想日历边界，因为 SQLite 处理 2100 年 localtime 时可能套用 2000 闰年规则 */
				nlegacy = 0;
				for (k = 0; k < 5; k++) {
					char label[11];
					int position = 0, j;
					monthend_legacy_sql_label(label, boundary + offsets[k]);
					while (position < nlegacy && strcmp(legacy[position].label, label) < 0) {
						position++;
					}
					if (position == nlegacy || strcmp(legacy[position].label, label) != 0) {
						for (j = nlegacy; j > position; j--) {
							legacy[j] = legacy[j - 1];
						}
						memset(&legacy[position], 0, sizeof(legacy[position]));
						strncpy_nt(legacy[position].label, label, sizeof(legacy[position].label));
						nlegacy++;
					}
					legacy[position].rx += sample_rx[k];
					legacy[position].tx += sample_tx[k];
				}
			}
			ck_assert_int_eq(db_getdata(&data, &info, "monthend0", "month", 0), 1);
			if (day <= 28) {
				dbdatalist *row = data;
				ck_assert_int_eq(info.count, nlegacy);
				for (k = 0; k < nlegacy; k++) {
					ck_assert_ptr_ne(row, NULL);
					ck_assert_int_eq(row->timestamp,
					                 monthend_label(atoi(legacy[k].label), atoi(legacy[k].label + 5)));
					ck_assert_int_eq(row->rx, legacy[k].rx);
					ck_assert_int_eq(row->tx, legacy[k].tx);
					row = row->next;
				}
				ck_assert_ptr_eq(row, NULL);
			} else {
				/* 29..31 日走自定义函数分支，必须严格按截断后的账期切点分账 */
				ck_assert_int_eq(info.count, 2);
				ck_assert_int_eq(data->timestamp, monthend_label(year - (month == 1), month == 1 ? 12 : month - 1));
				ck_assert_int_eq(data->rx, 11);
				ck_assert_int_eq(data->tx, 22);
				ck_assert_int_eq(data->next->timestamp, monthend_label(year, month));
				ck_assert_int_eq(data->next->rx, 83);
				ck_assert_int_eq(data->next->tx, 104);
			}
			dbdatalistfree(&data);
			ck_assert_int_eq(db_removeinterface("monthend0"), 1);
		}
	}
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_legacy_midnight_sql)
{
	sqlite3_stmt *statement;
	char sql[1024], nowdate[64];
	time_t boundary;
	int c, day, offset;

	monthend_timezone(_i);
	ck_assert_int_eq(db_open_rw(1), 1);
	for (c = 0; c < (int)(sizeof(monthend_cases) / sizeof(monthend_cases[0])); c++) {
		for (day = 1; day <= 28; day++) {
			cfg.monthrotate = day;
			cfg.monthrotatehour = cfg.monthrotateminute = 0;
			boundary = (time_t)get_timestamp(monthend_cases[c].year, monthend_cases[c].month, day, 0, 0);
			for (offset = -1; offset <= 1; offset++) {
				snprintf(nowdate, sizeof(nowdate), "datetime(%" PRIu64 ", 'unixepoch')", (uint64_t)(boundary + offset * 60));
				snprintf(sql, sizeof(sql), "select (%s) = strftime('%%Y-%%m-01', datetime(%s, '-%d days')%s)",
				         db_get_date_generator(3, 0, nowdate), nowdate, day - 1, cfg.dbtzmodifier);
				ck_assert_int_eq(sqlite3_prepare_v2(db, sql, -1, &statement, NULL), SQLITE_OK);
				ck_assert_int_eq(sqlite3_step(statement), SQLITE_ROW);
				ck_assert_int_eq(sqlite3_column_int(statement, 0), 1);
				ck_assert_int_eq(sqlite3_finalize(statement), SQLITE_OK);
			}
		}
	}
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_monthend_years_and_imports)
{
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	sqlite3_stmt *statement;
	time_t boundary, label;
	char sql[640], nowdate[64];
	int day, yearly, range;

	monthend_timezone(_i);
	ck_assert_int_eq(db_open_rw(1), 1);
	for (day = 29; day <= 31; day++) {
		cfg.monthrotate = day;
		cfg.monthrotatehour = 18;
		cfg.monthrotateminute = 24;
		label = monthend_label(2024, 1);
		boundary = (time_t)get_timestamp(2024, 1, day, 18, 24);
		for (yearly = 0; yearly <= 1; yearly++) {
			cfg.monthrotateyears = yearly;
			ck_assert_int_eq(billingperiodstart(label, 1, 0), yearly ? boundary : (time_t)get_timestamp(2024, 1, 1, 0, 0));
			ck_assert_int_eq(billingperiodstart(label, 1, 1), (time_t)get_timestamp(2025, 1, yearly ? day : 1, yearly ? 18 : 0, yearly ? 24 : 0));
			ck_assert_int_eq(db_addinterface("monthend0"), 1);
			ck_assert_int_eq(db_addtraffic_dated("monthend0", 10, 20, (uint64_t)(boundary - 60)), 1);
			ck_assert_int_eq(db_addtraffic_dated("monthend0", 30, 40, (uint64_t)boundary), 1);
			ck_assert_int_eq(db_getdata(&data, &info, "monthend0", "year", 0), 1);
			ck_assert_int_eq(info.count, yearly ? 2 : 1);
			ck_assert_int_eq(data->timestamp, monthend_label(yearly ? 2023 : 2024, 1));
			ck_assert_int_eq(data->rx, yearly ? 10 : 40);
			if (yearly) {
				ck_assert_int_eq(data->next->timestamp, label);
				ck_assert_int_eq(data->next->rx, 30);
			}
			dbdatalistfree(&data);
			ck_assert_int_eq(db_removeinterface("monthend0"), 1);
		}
		snprintf(nowdate, sizeof(nowdate), "datetime(%" PRIu64 ", 'unixepoch')", get_timestamp(2024, 2, 1, 0, 0));
		for (range = 3; range <= 4; range++) {
			snprintf(sql, sizeof(sql), "select %s", db_get_date_generator(range, 1, nowdate));
			ck_assert_int_eq(sqlite3_prepare_v2(db, sql, -1, &statement, NULL), SQLITE_OK);
			ck_assert_int_eq(sqlite3_step(statement), SQLITE_ROW);
			ck_assert_str_eq((const char *)sqlite3_column_text(statement, 0), range == 3 ? "2024-02-01" : "2024-01-01");
			ck_assert_int_eq(sqlite3_finalize(statement), SQLITE_OK);
		}
	}
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_monthend_cache_and_percentile)
{
	DSTATE state;
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	percentiledata pdata;
	time_t boundary, end, first, last;
	int day, i, year = 2023 + _i / 8;

	monthend_timezone(_i % 8);
	initdstate(&state);
	strcpy(cfg.dbdir, TESTDBDIR);
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = 24;
	ck_assert_int_eq(db_open_rw(1), 1);
	for (day = 29; day <= 31; day++) {
		cfg.monthrotate = day;
		boundary = (time_t)get_timestamp(year, 2, year == 2023 ? 28 : 29, 18, 24);
		end = (time_t)get_timestamp(year, 3, day, 18, 24);
		ck_assert_int_eq(db_addinterface("monthend0"), 1);
		ck_assert_int_eq(datacache_add(&state.dcache, "monthend0", 0), 1);
		state.dcache->updated = boundary - 20;
		for (i = 0; i < 3; i++) {
			ifinfo.timestamp = boundary + i * 20;
			ifinfo.rx = (uint64_t)((i + 1) * 10);
			ifinfo.tx = (uint64_t)((i + 1) * 20);
			ifinfo.is64bit = 1;
			ck_assert_int_eq(processifinfo(&state, &state.dcache), 1);
		}
		flushcachetodisk(&state);
		ck_assert_int_eq(db_errcode, 0);
		ck_assert_int_eq(db_getdata(&data, &info, "monthend0", "month", 0), 1);
		ck_assert_int_eq(info.count, 2);
		ck_assert_int_eq(data->timestamp, monthend_label(year, 1));
		ck_assert_int_eq(data->rx, 10);
		ck_assert_int_eq(data->next->timestamp, monthend_label(year, 2));
		ck_assert_int_eq(data->next->rx, 20);
		dbdatalistfree(&data);
		datacache_clear(&state.dcache);
		ck_assert_int_eq(db_removeinterface("monthend0"), 1);

		ck_assert_int_eq(db_addinterface("monthend0"), 1);
		first = boundary + 60;
		last = end - 540;
		ck_assert_int_eq(db_addtraffic_dated("monthend0", 9000, 9000, (uint64_t)(first - 300)), 1);
		ck_assert_int_eq(db_addtraffic_dated("monthend0", 1, 2, (uint64_t)first), 1);
		ck_assert_int_eq(db_addtraffic_dated("monthend0", 3, 6, (uint64_t)last), 1);
		ck_assert_int_eq(db_addtraffic_dated("monthend0", 9000, 9000, (uint64_t)(last + 300)), 1);
		ck_assert_int_eq(db_setupdated("monthend0", end - 1), 1);
		ck_assert_msg(getpercentiledata(&pdata, "monthend0", 0) == 1, "%s", errorstring);
		ck_assert_int_eq(pdata.monthbegin, monthend_querytime(boundary));
		ck_assert_int_eq(pdata.count, 2);
		ck_assert_int_eq(pdata.sumrx, 4);
		ck_assert_int_eq(pdata.sumtx, 8);
		ck_assert_int_eq(db_removeinterface("monthend0"), 1);
	}
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_monthend_dst_transitions)
{
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	time_t boundary, label;
	int year = _i / 2 ? 2021 : 2024, month = _i / 2 ? 10 : 3;

	setenv("TZ", "Europe/Berlin", 1);
	tzset();
	cfg.useutc = _i % 2;
	cfg.monthrotate = 31;
	cfg.monthrotatehour = 2;
	cfg.monthrotateminute = 24;
	validatecfg(CT_All);
	/* 遇到夏令时跳变或重复的本地时刻，统一按 libc 的 tm_isdst=-1 处理 */
	label = monthend_label(year, month);
	boundary = (time_t)get_timestamp(year, month, 31, 2, 24);
	ck_assert_int_eq(billingperiodstart(label, 0, 0), boundary);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("monthend0"), 1);
	ck_assert_int_eq(db_addtraffic_dated("monthend0", 10, 20, (uint64_t)(boundary - 60)), 1);
	ck_assert_int_eq(db_addtraffic_dated("monthend0", 30, 40, (uint64_t)boundary), 1);
	ck_assert_int_eq(db_getdata(&data, &info, "monthend0", "month", 0), 1);
	ck_assert_int_eq(info.count, 2);
	ck_assert_int_eq(data->timestamp, monthend_label(year, month - 1));
	ck_assert_int_eq(data->rx, 10);
	ck_assert_int_eq(data->next->timestamp, label);
	ck_assert_int_eq(data->next->rx, 30);
	dbdatalistfree(&data);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_minute_database_boundary)
{
	const char *zones[] = {"UTC", "Asia/Singapore", "America/New_York"};
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	time_t boundary;
	int minute;

	setenv("TZ", zones[_i / 2], 1);
	tzset();
	cfg.useutc = _i % 2;
	validatecfg(CT_All);
	cfg.monthrotate = 7;
	cfg.monthrotatehour = 18;
	cfg.monthrotateyears = 1;
	ck_assert_int_eq(db_open_rw(1), 1);
	for (minute = 0; minute < 60; minute++) {
		cfg.monthrotateminute = minute;
		ck_assert_int_eq(db_addinterface("minute0"), 1);
		boundary = (time_t)get_timestamp(2024, 1, 7, 18, minute);
		ck_assert_int_eq(db_addtraffic_dated("minute0", 10, 20, (uint64_t)(boundary - 1)), 1);
		ck_assert_int_eq(db_addtraffic_dated("minute0", 30, 40, (uint64_t)boundary), 1);
		ck_assert_int_eq(db_addtraffic_dated("minute0", 50, 60, (uint64_t)(boundary + 1)), 1);
		ck_assert_int_eq(db_getdata(&data, &info, "minute0", "month", 0), 1);
		ck_assert_int_eq(info.count, 2);
		ck_assert_int_eq(data->rx, 10);
		ck_assert_int_eq(data->tx, 20);
		ck_assert_int_eq(data->next->rx, 80);
		ck_assert_int_eq(data->next->tx, 100);
		dbdatalistfree(&data);
		ck_assert_int_eq(db_getdata(&data, &info, "minute0", "year", 0), 1);
		ck_assert_int_eq(info.count, 2);
		ck_assert_int_eq(data->rx, 10);
		ck_assert_int_eq(data->next->rx, 80);
		dbdatalistfree(&data);
		ck_assert_int_eq(db_removeinterface("minute0"), 1);
	}
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_minute_cache_separates_periods)
{
	const char *zones[] = {"UTC", "Asia/Singapore", "America/New_York"};
	DSTATE state;
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	interfaceinfo totals;
	time_t boundary, step;
	int minute, i;

	initdstate(&state);
	strcpy(cfg.dbdir, TESTDBDIR);
	setenv("TZ", zones[_i / 2], 1);
	tzset();
	cfg.useutc = _i % 2;
	validatecfg(CT_All);
	cfg.monthrotate = 7;
	cfg.monthrotatehour = 18;
	cfg.monthrotateyears = 1;
	ck_assert_int_eq(db_open_rw(1), 1);
	for (minute = 0; minute < 60; minute++) {
		cfg.monthrotateminute = minute;
		boundary = (time_t)get_timestamp(2024, 1, 7, 18, minute);
		ck_assert_int_eq(db_addinterface("minute0"), 1);
		ck_assert_int_eq(datacache_add(&state.dcache, "minute0", 0), 1);
		state.dcache->updated = boundary - 20;
		for (i = 0; i < 3; i++) {
			step = boundary + i * 20;
			ifinfo.timestamp = step;
			ifinfo.rx = (uint64_t)((i + 1) * 10);
			ifinfo.tx = (uint64_t)((i + 1) * 20);
			ifinfo.is64bit = 1;
			ck_assert_int_eq(processifinfo(&state, &state.dcache), 1);
		}
		ck_assert_ptr_ne(state.dcache->log, NULL);
		ck_assert_int_eq(state.dcache->log->timestamp,
			boundary - (boundary % (minute % 5 ? 60 : 300)));
		flushcachetodisk(&state);
		ck_assert_int_eq(db_errcode, 0);
		ck_assert_ptr_eq(state.dcache->log, NULL);
		flushcachetodisk(&state);
		ck_assert_int_eq(db_errcode, 0);
		ck_assert_int_eq(db_getdata(&data, &info, "minute0", "month", 0), 1);
		ck_assert_int_eq(info.count, 2);
		ck_assert_int_eq(data->rx, 10);
		ck_assert_int_eq(data->tx, 20);
		ck_assert_int_eq(data->next->rx, 20);
		ck_assert_int_eq(data->next->tx, 40);
		dbdatalistfree(&data);
		ck_assert_int_eq(db_getdata(&data, &info, "minute0", "year", 0), 1);
		ck_assert_int_eq(info.count, 2);
		ck_assert_int_eq(data->rx, 10);
		ck_assert_int_eq(data->next->rx, 20);
		dbdatalistfree(&data);
		ck_assert_int_eq(db_getdata(&data, &info, "minute0", "fiveminute", 0), 1);
		ck_assert_int_eq(info.count, minute % 5 ? 1 : 2);
		ck_assert_int_eq(info.sumrx, 30);
		ck_assert_int_eq(info.sumtx, 60);
		dbdatalistfree(&data);
		ck_assert_int_eq(db_getinterfaceinfo_epoch("minute0", &totals), 1);
		ck_assert_int_eq(totals.rxtotal, 30);
		ck_assert_int_eq(totals.txtotal, 60);
		datacache_clear(&state.dcache);
		ck_assert_int_eq(db_removeinterface("minute0"), 1);
	}
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_minute_straddling_sample_keeps_upstream_attribution)
{
	DSTATE state;
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	time_t boundary;

	initdstate(&state);
	strcpy(cfg.dbdir, TESTDBDIR);
	cfg.monthrotate = 7;
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = _i;
	boundary = (time_t)get_timestamp(2024, 1, 7, 18, _i);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("minute0"), 1);
	ck_assert_int_eq(datacache_add(&state.dcache, "minute0", 0), 1);
	state.dcache->updated = boundary - 10;
	ifinfo.timestamp = boundary + 10;
	ifinfo.rx = 50;
	ifinfo.tx = 70;
	ifinfo.is64bit = 1;
	ck_assert_int_eq(processifinfo(&state, &state.dcache), 1);
	ifinfo.timestamp = boundary + 30;
	ifinfo.rx = 80;
	ifinfo.tx = 110;
	ck_assert_int_eq(processifinfo(&state, &state.dcache), 1);
	flushcachetodisk(&state);
	ck_assert_int_eq(db_errcode, 0);
	ck_assert_int_eq(db_getdata(&data, &info, "minute0", "month", 0), 1);
	ck_assert_int_eq(info.count, 2);
	/* 单次采样区间如果跨越账期切点，内核计数器拿不到包级时间，仍随区间起点归入上一期 */
	ck_assert_int_eq(data->rx, 50);
	ck_assert_int_eq(data->tx, 70);
	ck_assert_int_eq(data->next->rx, 30);
	ck_assert_int_eq(data->next->tx, 40);
	ck_assert_int_eq(info.sumrx, 80);
	ck_assert_int_eq(info.sumtx, 110);
	dbdatalistfree(&data);
	datacache_clear(&state.dcache);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_database_boundary)
{
	const char *zones[] = {"UTC", "Asia/Singapore", "America/New_York"};
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	time_t boundary;
	char label[32];

	setenv("TZ", zones[_i / 2], 1);
	tzset();
	cfg.useutc = _i % 2;
	validatecfg(CT_All);
	cfg.monthrotate = 7;
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = 25;
	cfg.monthrotateyears = 1;
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("billing0"), 1);
	boundary = (time_t)get_timestamp(2024, 1, 7, 18, 25);
	ck_assert_int_eq(db_addtraffic_dated("billing0", 10, 20, (uint64_t)(boundary - 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("billing0", 30, 40, (uint64_t)boundary), 1);
	ck_assert_int_eq(db_addtraffic_dated("billing0", 50, 60, (uint64_t)(boundary + 300)), 1);
	ck_assert_int_eq(db_getdata(&data, &info, "billing0", "month", 0), 1);
	ck_assert_int_eq(info.count, 2);
	ck_assert_int_eq(data->rx, 10);
	strftime(label, sizeof(label), "%Y-%m-%d", localtime(&data->timestamp));
	ck_assert_str_eq(label, "2023-12-01");
	ck_assert_int_eq(data->next->rx, 80);
	ck_assert_int_eq(data->next->tx, 100);
	strftime(label, sizeof(label), "%Y-%m-%d", localtime(&data->next->timestamp));
	ck_assert_str_eq(label, "2024-01-01");
	/* 查询接口返回的时间戳统一走日历时间（含 UseUTC 模式） */
	ck_assert_int_eq(getperiodseconds(LT_Month, data->next->timestamp,
	                                data->next->timestamp + 7 * 86400, 0, 0), 31 * 86400);
	dbdatalistfree(&data);
	ck_assert_int_eq(db_getdata(&data, &info, "billing0", "year", 0), 1);
	ck_assert_int_eq(info.count, 2);
	ck_assert_int_eq(data->rx, 10);
	ck_assert_int_eq(data->next->rx, 80);
	dbdatalistfree(&data);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_first_day_nonmidnight)
{
	dbdatalist *data = NULL;
	dbdatalistinfo info;

	cfg.monthrotatehour = 1;
	cfg.monthrotateminute = 5;
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("billing0"), 1);
	ck_assert_int_eq(db_addtraffic_dated("billing0", 10, 20, get_timestamp(2024, 1, 1, 1, 0)), 1);
	ck_assert_int_eq(db_addtraffic_dated("billing0", 30, 40, get_timestamp(2024, 1, 1, 1, 5)), 1);
	ck_assert_int_eq(db_getdata(&data, &info, "billing0", "month", 0), 1);
	ck_assert_int_eq(info.count, 2);
	ck_assert_int_eq(data->rx, 10);
	ck_assert_int_eq(data->next->rx, 30);
	dbdatalistfree(&data);
	/* 没开 MonthRotateAffectsYears 时，年表还是按自然年切分 */
	ck_assert_int_eq(db_getdata(&data, &info, "billing0", "year", 0), 1);
	ck_assert_int_eq(info.count, 1);
	ck_assert_int_eq(data->rx, 40);
	dbdatalistfree(&data);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_periods_and_estimates)
{
	time_t label, start, end;
	uint64_t rx, tx;
	dbdatalist *data = NULL;

	cfg.monthrotate = 7;
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = 24;
	label = (time_t)get_timestamp(2024, 2, 1, 0, 0);
	start = (time_t)get_timestamp(2024, 2, 7, 18, 24);
	end = (time_t)get_timestamp(2024, 3, 7, 18, 24);
	ck_assert_int_eq(billingperiodstart(label, 0, 0), start);
	ck_assert_int_eq(billingperiodstart(label, 0, 1), end);
	ck_assert_int_eq(issametimeslot(LT_Month, label, start - 1), 0);
	ck_assert_int_eq(issametimeslot(LT_Month, label, start), 1);
	ck_assert_int_eq(issametimeslot(LT_Month, label, end - 1), 1);
	ck_assert_int_eq(issametimeslot(LT_Month, label, end), 0);
	ck_assert_int_eq(mosecs(label, start - 1), 1);
	ck_assert_int_eq(mosecs(label, start + 300), 300);
	ck_assert_int_eq(getperiodseconds(LT_Month, label, start + 600, start + 300, 1), 300);
	ck_assert_int_eq(getperiodseconds(LT_Month, label, end, 0, 0), 29 * 86400);
	ck_assert_int_eq(dbdatalistadd(&data, 1000, 2000, label, 1), 1);
	getestimates(&rx, &tx, LT_Month, start + 1000, 0, &data);
	ck_assert_int_eq(rx, 29 * 86400);
	ck_assert_int_eq(tx, 2 * 29 * 86400);
	dbdatalistfree(&data);
	/* 2 月天数必须按记录所属年份算，不能偷懒用当前系统年份 */
	label = (time_t)get_timestamp(2023, 2, 1, 0, 0);
	ck_assert_int_eq(getperiodseconds(LT_Month, label, 0, 0, 0), 28 * 86400);
	/* 本地账期跨夏令时时保持钟面时分不变，实际总秒数会少 1 小时 */
	setenv("TZ", "America/New_York", 1);
	tzset();
	label = (time_t)get_timestamp(2024, 3, 1, 0, 0);
	ck_assert_int_eq(getperiodseconds(LT_Month, label, 0, 0, 0), 31 * 86400 - 3600);
}
END_TEST

START_TEST(billing_direct_import_preserves_labels)
{
	const char *sql;

	cfg.monthrotate = 7;
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = 25;
	cfg.monthrotateyears = 1;
	sql = db_get_date_generator(3, 1, "'2024-01-01'");
	ck_assert_ptr_eq(strstr(sql, "minutes"), NULL);
	sql = db_get_date_generator(4, 1, "'2024-01-01'");
	ck_assert_ptr_eq(strstr(sql, "minutes"), NULL);
}
END_TEST

START_TEST(billing_cache_uses_real_epoch)
{
	const char *zones[] = {"UTC", "Asia/Singapore", "America/New_York"};
	DSTATE state;
	time_t timestamp;

	initdstate(&state);
	strcpy(cfg.dbdir, TESTDBDIR);
	setenv("TZ", zones[_i / 4], 1);
	tzset();
	cfg.useutc = _i % 2;
	validatecfg(CT_All);
	timestamp = (time_t)get_timestamp(2024, (_i % 4 < 2) ? 1 : 7, 7, 18, 25);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("billing0"), 1);
	ck_assert_int_eq(db_setupdated("billing0", timestamp), 1);
	ck_assert_int_eq(datacache_add(&state.dcache, "billing0", 0), 1);
	ck_assert_int_eq(initcachevalues(&state, &state.dcache), 1);
	ck_assert_int_eq(state.dcache->updated, timestamp);
	datacache_clear(&state.dcache);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_cache_flush_respects_boundary)
{
	DSTATE state;
	dbdatalist *data = NULL;
	dbdatalistinfo info;
	time_t boundary;

	initdstate(&state);
	strcpy(cfg.dbdir, TESTDBDIR);
	cfg.monthrotate = 7;
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = 25;
	boundary = (time_t)get_timestamp(2024, 1, 7, 18, 25);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("billing0"), 1);
	ck_assert_int_eq(datacache_add(&state.dcache, "billing0", 0), 1);
	state.dcache->updated = boundary + 300;
	state.dcache->currx = 40;
	state.dcache->curtx = 60;
	ck_assert_int_eq(xferlog_add(&state.dcache->log, boundary - 300, 10, 20), 1);
	ck_assert_int_eq(xferlog_add(&state.dcache->log, boundary, 30, 40), 1);
	flushcachetodisk(&state);
	ck_assert_int_eq(db_errcode, 0);
	ck_assert_ptr_eq(state.dcache->log, NULL);
	ck_assert_int_eq(db_getdata(&data, &info, "billing0", "month", 0), 1);
	ck_assert_int_eq(info.count, 2);
	ck_assert_int_eq(data->rx, 10);
	ck_assert_int_eq(data->next->rx, 30);
	dbdatalistfree(&data);
	datacache_clear(&state.dcache);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_utc_ignores_local_dst_duration)
{
	struct tm labeltm;
	time_t label;

	setenv("TZ", "America/New_York", 1);
	tzset();
	cfg.useutc = 1;
	cfg.monthrotate = 7;
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = 25;
	memset(&labeltm, 0, sizeof(labeltm));
	labeltm.tm_year = 2024 - 1900;
	labeltm.tm_mon = 2;
	labeltm.tm_mday = 1;
	labeltm.tm_isdst = -1;
	label = mktime(&labeltm);
	ck_assert_int_eq(getperiodseconds(LT_Month, label, 0, 0, 0), 31 * 86400);
}
END_TEST

START_TEST(billing_monthrotatenote_and_visibility)
{
	char note[96];

	cfg.monthrotatevisible = 1;
	cfg.monthrotate = 1;
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = 24;
	ck_assert_int_eq(ismonthrotatenoteneeded(), 1);
	getmonthrotatenote(note, sizeof(note));
	ck_assert_str_eq(note, "Monthly data begins on the 1st at 18:24 of each indicated month.");

	cfg.monthrotate = 31;
	getmonthrotatenote(note, sizeof(note));
	ck_assert_str_eq(note, "Monthly data begins on the 31st at 18:24 of each indicated month.");

	cfg.monthrotate = 22;
	cfg.monthrotatehour = 0;
	cfg.monthrotateminute = 5;
	getmonthrotatenote(note, sizeof(note));
	ck_assert_str_eq(note, "Monthly data begins on the 22nd at 00:05 of each indicated month.");

	cfg.monthrotate = 3;
	cfg.monthrotatehour = 0;
	cfg.monthrotateminute = 0;
	ck_assert_int_eq(ismonthrotatenoteneeded(), 1);
	getmonthrotatenote(note, sizeof(note));
	ck_assert_str_eq(note, "Monthly data begins on the 3rd of each indicated month.");

	cfg.monthrotate = 1;
	ck_assert_int_eq(ismonthrotatenoteneeded(), 0);
}
END_TEST

START_TEST(billing_historical_month_summary_rate)
{
	char buffer[4096];
	int output, count;

	cfg.ostyle = 3;
	cfg.rateunit = 0;
	cfg.unitmode = 0;
	cfg.monthrotate = 1;
	cfg.monthrotatehour = 0;
	cfg.monthrotateminute = 0;
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("rate0"), 1);
	/* 2024 年 2 月有 29 天（2505600 秒），不能按当前系统年份的 28 天来算 */
	ck_assert_int_eq(db_addtraffic_dated("rate0", 2505600ULL * 2048ULL, 0, get_timestamp(2024, 2, 15, 0, 0)), 1);
	ck_assert_int_eq(db_addtraffic_dated("rate0", 1024, 0, get_timestamp(2024, 3, 15, 0, 0)), 1);
	ck_assert_int_eq(db_setupdated("rate0", (time_t)get_timestamp(2024, 3, 15, 0, 0)), 1);
	output = pipe_output();
	showdb("rate0", 2, "", "");
	fflush(stdout);
	count = (int)read(output, buffer, sizeof(buffer) - 1);
	ck_assert_int_gt(count, 0);
	buffer[count] = '\0';
	ck_assert_ptr_ne(strstr(buffer, "2.00 KiB/s"), NULL);
	ck_assert_int_eq(close(output), 0);
	ck_assert_int_eq(db_removeinterface("rate0"), 1);

	/* 31 号账期下，2024 年 2 月账期（2/29 18:24 ~ 3/31 18:24）实际是 31 天（2678400 秒） */
	cfg.monthrotate = 31;
	cfg.monthrotatehour = 18;
	cfg.monthrotateminute = 24;
	ck_assert_int_eq(db_addinterface("rate1"), 1);
	ck_assert_int_eq(db_addtraffic_dated("rate1", 2678400ULL * 2048ULL, 0, get_timestamp(2024, 2, 29, 18, 24)), 1);
	ck_assert_int_eq(db_addtraffic_dated("rate1", 1024, 0, get_timestamp(2024, 3, 31, 18, 24)), 1);
	ck_assert_int_eq(db_setupdated("rate1", (time_t)get_timestamp(2024, 3, 31, 18, 24)), 1);
	output = pipe_output();
	showdb("rate1", 2, "", "");
	fflush(stdout);
	count = (int)read(output, buffer, sizeof(buffer) - 1);
	ck_assert_int_gt(count, 0);
	buffer[count] = '\0';
	ck_assert_ptr_ne(strstr(buffer, "2.00 KiB/s"), NULL);
	ck_assert_int_eq(close(output), 0);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(billing_percentile_extreme_userlimit_does_not_overflow)
{
	percentiledata pdata;

	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("billing0"), 1);
	ck_assert_int_eq(db_addtraffic_dated("billing0", 1000, 2000, get_timestamp(2024, 1, 1, 0, 0)), 1);
	ck_assert_int_eq(getpercentiledata(&pdata, "billing0", UINT64_MAX), 1);
	ck_assert_int_eq(pdata.countrxoveruserlimit, 0);
	ck_assert_int_eq(pdata.counttxoveruserlimit, 0);
	ck_assert_int_eq(pdata.countsumoveruserlimit, 0);
	ck_assert_int_eq(getpercentiledata(&pdata, "billing0", UINT64_MAX / 300 + 1), 1);
	ck_assert_int_eq(pdata.countrxoveruserlimit, 0);
	ck_assert_int_eq(pdata.counttxoveruserlimit, 0);
	ck_assert_int_eq(pdata.countsumoveruserlimit, 0);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

void add_billing_tests(Suite *s)
{
	TCase *tc = tcase_create("Billing");

	tcase_add_checked_fixture(tc, billing_fixture, teardown);
	tcase_add_test(tc, billing_config_validation);
	tcase_add_loop_test(tc, billing_monthend_periods, 0, 8);
	tcase_add_loop_test(tc, billing_monthend_database_labels, 0, 96);
	tcase_add_loop_test(tc, billing_legacy_midnight_sql, 0, 8);
	tcase_add_loop_test(tc, billing_monthend_years_and_imports, 0, 8);
	tcase_add_loop_test(tc, billing_monthend_cache_and_percentile, 0, 16);
	tcase_add_loop_test(tc, billing_monthend_dst_transitions, 0, 4);
	tcase_add_loop_test(tc, billing_minute_database_boundary, 0, 6);
	tcase_add_loop_test(tc, billing_minute_cache_separates_periods, 0, 6);
	tcase_add_loop_test(tc, billing_minute_straddling_sample_keeps_upstream_attribution, 0, 60);
	tcase_add_loop_test(tc, billing_database_boundary, 0, 6);
	tcase_add_test(tc, billing_first_day_nonmidnight);
	tcase_add_test(tc, billing_periods_and_estimates);
	tcase_add_test(tc, billing_direct_import_preserves_labels);
	tcase_add_loop_test(tc, billing_cache_uses_real_epoch, 0, 12);
	tcase_add_test(tc, billing_cache_flush_respects_boundary);
	tcase_add_test(tc, billing_utc_ignores_local_dst_duration);
	tcase_add_test(tc, billing_monthrotatenote_and_visibility);
	tcase_add_test(tc, billing_historical_month_summary_rate);
	tcase_add_test(tc, billing_percentile_extreme_userlimit_does_not_overflow);
	suite_add_tcase(s, tc);
}
