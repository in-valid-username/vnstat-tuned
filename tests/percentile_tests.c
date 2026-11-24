#include "common.h"
#include "vnstat_tests.h"
#include "percentile_tests.h"
#include "dbsql.h"
#include "percentile.h"

START_TEST(compare_uint64_t_can_compare)
{
	uint64_t a = 42, b = 42;

	ck_assert_int_eq(compare_uint64_t(&a, &b), 0);
	a += 10;
	ck_assert_int_eq(compare_uint64_t(&a, &b), 1);
	b += 20;
	ck_assert_int_eq(compare_uint64_t(&a, &b), -1);
	a += 10;
	ck_assert_int_eq(compare_uint64_t(&a, &b), 0);
	a = 0;
	b = 0;
	ck_assert_int_eq(compare_uint64_t(&a, &b), 0);
}
END_TEST

START_TEST(compare_uint64_t_can_be_used_with_qsort)
{
	uint64_t l[5] = {123, 2, 0, 3, 1};

	qsort((void *)l, 5, sizeof(uint64_t), compare_uint64_t);

	ck_assert_int_eq(l[0], 0);
	ck_assert_int_eq(l[1], 1);
	ck_assert_int_eq(l[2], 2);
	ck_assert_int_eq(l[3], 3);
	ck_assert_int_eq(l[4], 123);
}
END_TEST

START_TEST(getpercentiledata_returns_with_no_wrong_interface)
{
	int ret;
	percentiledata pdata;

	pdata.userlimitbytespersecond = 1;
	pdata.countrxoveruserlimit = 2;
	pdata.counttxoveruserlimit = 3;
	pdata.countsumoveruserlimit = 4;

	ret = db_open_rw(1);
	ck_assert_int_eq(ret, 1);

	suppress_output();

	ret = getpercentiledata(&pdata, "no_interface", 1234);
	ck_assert_int_eq(ret, 0);
	ck_assert_int_eq(pdata.userlimitbytespersecond, 1234);
	ck_assert_int_eq(pdata.countrxoveruserlimit, 0);
	ck_assert_int_eq(pdata.counttxoveruserlimit, 0);
	ck_assert_int_eq(pdata.countsumoveruserlimit, 0);
	ck_assert_str_eq(errorstring, "Failed to fetch month data for 95th percentile.");

	ret = db_close();
	ck_assert_int_eq(ret, 1);
}
END_TEST

START_TEST(getpercentiledata_returns_with_no_data)
{
	int ret;
	percentiledata pdata;

	pdata.userlimitbytespersecond = 1;
	pdata.countrxoveruserlimit = 2;
	pdata.counttxoveruserlimit = 3;
	pdata.countsumoveruserlimit = 4;

	ret = db_open_rw(1);
	ck_assert_int_eq(ret, 1);
	ret = db_addinterface("interface");
	ck_assert_int_eq(ret, 1);

	suppress_output();

	ret = getpercentiledata(&pdata, "interface", 1234);
	ck_assert_int_eq(ret, 0);
	ck_assert_int_eq(pdata.userlimitbytespersecond, 1234);
	ck_assert_int_eq(pdata.countrxoveruserlimit, 0);
	ck_assert_int_eq(pdata.counttxoveruserlimit, 0);
	ck_assert_int_eq(pdata.countsumoveruserlimit, 0);
	ck_assert_str_eq(errorstring, "No month data for 95th percentile available.");

	ret = db_close();
	ck_assert_int_eq(ret, 1);
}
END_TEST

START_TEST(getpercentiledata_can_provide_data_with_one_entry)
{
	int ret;
	uint64_t entry;
	percentiledata pdata;

	entry = get_timestamp(2001, 1, 1, 0, 0);

	pdata.userlimitbytespersecond = 1;
	pdata.countrxoveruserlimit = 2;
	pdata.counttxoveruserlimit = 3;
	pdata.countsumoveruserlimit = 4;

	ret = db_open_rw(1);
	ck_assert_int_eq(ret, 1);
	ret = db_addinterface("interface");
	ck_assert_int_eq(ret, 1);

	ret = db_addtraffic_dated("interface", 1, 2, entry);
	ck_assert_int_eq(ret, 1);

	ret = db_setupdated("interface", (time_t)entry);
	ck_assert_int_eq(ret, 1);

	suppress_output();

	ret = getpercentiledata(&pdata, "interface", 0);
	ck_assert_int_eq(ret, 1);
	ck_assert_int_eq(pdata.userlimitbytespersecond, 0);
	ck_assert_int_eq(pdata.countrxoveruserlimit, 0);
	ck_assert_int_eq(pdata.counttxoveruserlimit, 0);
	ck_assert_int_eq(pdata.countsumoveruserlimit, 0);
	ck_assert_int_eq(pdata.count, 1);
	ck_assert_int_eq(pdata.countexpectation, 1);
	ck_assert_int_eq(pdata.rxpercentile, 1);
	ck_assert_int_eq(pdata.txpercentile, 2);
	ck_assert_int_eq(pdata.sumpercentile, 3);
	ck_assert_int_eq(pdata.maxrx, 1);
	ck_assert_int_eq(pdata.maxtx, 2);
	ck_assert_int_eq(pdata.max, 3);
	ck_assert_int_eq(pdata.minrx, 1);
	ck_assert_int_eq(pdata.mintx, 2);
	ck_assert_int_eq(pdata.min, 3);
	ck_assert_int_eq(pdata.sumrx, 1);
	ck_assert_int_eq(pdata.sumtx, 2);

	ret = db_close();
	ck_assert_int_eq(ret, 1);
}
END_TEST

START_TEST(getpercentiledata_can_provide_data_with_many_entries)
{
	int ret;
	uint64_t entry, i;
	percentiledata pdata;

	entry = get_timestamp(2001, 1, 1, 0, 0);

	pdata.userlimitbytespersecond = 1;
	pdata.countrxoveruserlimit = 2;
	pdata.counttxoveruserlimit = 3;
	pdata.countsumoveruserlimit = 4;

	ret = db_open_rw(1);
	ck_assert_int_eq(ret, 1);
	ret = db_addinterface("interface");
	ck_assert_int_eq(ret, 1);

	for (i = 0; i < 100; i++) {
		ret = db_addtraffic_dated("interface", i * 1, i * 2, entry + i * 300);
		ck_assert_int_eq(ret, 1);
	}

	ret = db_setupdated("interface", (time_t)(entry + i * 300));
	ck_assert_int_eq(ret, 1);

	suppress_output();

	ret = getpercentiledata(&pdata, "interface", 0);
	ck_assert_int_eq(ret, 1);
	ck_assert_int_eq(pdata.userlimitbytespersecond, 0);
	ck_assert_int_eq(pdata.countrxoveruserlimit, 0);
	ck_assert_int_eq(pdata.counttxoveruserlimit, 0);
	ck_assert_int_eq(pdata.countsumoveruserlimit, 0);
	ck_assert_int_eq(pdata.count, 100);
	ck_assert_int_eq(pdata.countexpectation, 100);
	ck_assert_int_eq(pdata.rxpercentile, 94);
	ck_assert_int_eq(pdata.txpercentile, 188);
	ck_assert_int_eq(pdata.sumpercentile, 282);
	ck_assert_int_eq(pdata.maxrx, 99);
	ck_assert_int_eq(pdata.maxtx, 198);
	ck_assert_int_eq(pdata.max, 297);
	ck_assert_int_eq(pdata.minrx, 0);
	ck_assert_int_eq(pdata.mintx, 0);
	ck_assert_int_eq(pdata.min, 0);
	ck_assert_int_eq(pdata.sumrx, 4950);
	ck_assert_int_eq(pdata.sumtx, 9900);

	ret = db_close();
	ck_assert_int_eq(ret, 1);
}
END_TEST

START_TEST(getpercentiledata_can_provide_data_with_many_entries_and_order_does_not_matter)
{
	int ret;
	uint64_t entry, i;
	percentiledata pdata;

	entry = get_timestamp(2001, 1, 1, 0, 0);

	pdata.userlimitbytespersecond = 1;
	pdata.countrxoveruserlimit = 2;
	pdata.counttxoveruserlimit = 3;
	pdata.countsumoveruserlimit = 4;

	ret = db_open_rw(1);
	ck_assert_int_eq(ret, 1);
	ret = db_addinterface("interface");
	ck_assert_int_eq(ret, 1);

	for (i = 0; i < 100; i++) {
		ret = db_addtraffic_dated("interface", (99 - i) * 1, (99 - i) * 2, entry + i * 300);
		ck_assert_int_eq(ret, 1);
	}

	ret = db_setupdated("interface", (time_t)(entry + i * 300));
	ck_assert_int_eq(ret, 1);

	suppress_output();

	ret = getpercentiledata(&pdata, "interface", 0);
	ck_assert_int_eq(ret, 1);
	ck_assert_int_eq(pdata.userlimitbytespersecond, 0);
	ck_assert_int_eq(pdata.countrxoveruserlimit, 0);
	ck_assert_int_eq(pdata.counttxoveruserlimit, 0);
	ck_assert_int_eq(pdata.countsumoveruserlimit, 0);
	ck_assert_int_eq(pdata.count, 100);
	ck_assert_int_eq(pdata.countexpectation, 100);
	ck_assert_int_eq(pdata.rxpercentile, 94);
	ck_assert_int_eq(pdata.txpercentile, 188);
	ck_assert_int_eq(pdata.sumpercentile, 282);
	ck_assert_int_eq(pdata.maxrx, 99);
	ck_assert_int_eq(pdata.maxtx, 198);
	ck_assert_int_eq(pdata.max, 297);
	ck_assert_int_eq(pdata.minrx, 0);
	ck_assert_int_eq(pdata.mintx, 0);
	ck_assert_int_eq(pdata.min, 0);
	ck_assert_int_eq(pdata.sumrx, 4950);
	ck_assert_int_eq(pdata.sumtx, 9900);

	ret = db_close();
	ck_assert_int_eq(ret, 1);
}
END_TEST

START_TEST(getpercentiledata_can_check_limit)
{
	int ret;
	uint64_t entry, i;
	percentiledata pdata;

	entry = get_timestamp(2001, 1, 1, 0, 0);

	pdata.userlimitbytespersecond = 1;
	pdata.countrxoveruserlimit = 2;
	pdata.counttxoveruserlimit = 3;
	pdata.countsumoveruserlimit = 4;

	ret = db_open_rw(1);
	ck_assert_int_eq(ret, 1);
	ret = db_addinterface("interface");
	ck_assert_int_eq(ret, 1);

	for (i = 0; i < 100; i++) {
		ret = db_addtraffic_dated("interface", i * 300, i * 600, entry + i * 300);
		ck_assert_int_eq(ret, 1);
	}

	ret = db_setupdated("interface", (time_t)(entry + i * 300));
	ck_assert_int_eq(ret, 1);

	suppress_output();
	debug = 1;

	ret = getpercentiledata(&pdata, "interface", 20);
	ck_assert_int_eq(ret, 1);
	ck_assert_int_eq(pdata.userlimitbytespersecond, 20);
	ck_assert_int_eq(pdata.countrxoveruserlimit, 79);
	ck_assert_int_eq(pdata.counttxoveruserlimit, 89);
	ck_assert_int_eq(pdata.countsumoveruserlimit, 93);
	ck_assert_int_eq(pdata.count, 100);
	ck_assert_int_eq(pdata.countexpectation, 100);
	ck_assert_int_eq(pdata.rxpercentile, 94 * 300);
	ck_assert_int_eq(pdata.txpercentile, 94 * 600);
	ck_assert_int_eq(pdata.sumpercentile, 84600);
	ck_assert_int_eq(pdata.maxrx, 99 * 300);
	ck_assert_int_eq(pdata.maxtx, 99 * 600);
	ck_assert_int_eq(pdata.max, 89100);
	ck_assert_int_eq(pdata.minrx, 0);
	ck_assert_int_eq(pdata.mintx, 0);
	ck_assert_int_eq(pdata.min, 0);
	ck_assert_int_eq(pdata.sumrx, 1485000);
	ck_assert_int_eq(pdata.sumtx, 2970000);

	ret = db_close();
	ck_assert_int_eq(ret, 1);
}
END_TEST

START_TEST(getpercentiledata_respects_both_billing_cutoffs)
{
	const int days[] = {1, 1, 10, 28};
	const int hours[] = {0, 12, 12, 23};
	const int minutes[] = {0, 5, 5, 55};
	time_t start, end;
	percentiledata pdata;

	cfg.monthrotate = days[_i];
	cfg.monthrotatehour = hours[_i];
	cfg.monthrotateminute = minutes[_i];
	start = (time_t)get_timestamp(2020, 1, days[_i], hours[_i], minutes[_i]);
	end = (time_t)get_timestamp(2020, 2, days[_i], hours[_i], minutes[_i]);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("interface"), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)(start - 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 1, 2, (uint64_t)start), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 2, 4, (uint64_t)(start + 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 3, 6, (uint64_t)(end - 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)end), 1);
	/* 即使月表存在脏标签，也不能把下个账期的 5 分钟样本捞进来 */
	ck_assert_int_eq(db_exec("DELETE FROM month WHERE date >= '2020-02-01'"), 1);
	ck_assert_int_eq(db_setupdated("interface", end), 1);
	ck_assert_int_eq(getpercentiledata(&pdata, "interface", 0), 1);
	ck_assert_int_eq(pdata.monthbegin, start);
	ck_assert_int_eq(pdata.databegin, start);
	ck_assert_int_eq(pdata.dataend, end - 300);
	ck_assert_int_eq(pdata.count, 3);
	ck_assert_int_eq(pdata.countexpectation, (end - start) / 300);
	ck_assert_int_eq(pdata.sumrx, 6);
	ck_assert_int_eq(pdata.sumtx, 12);
	ck_assert_int_eq(pdata.rxpercentile, 3);
	ck_assert_int_eq(pdata.txpercentile, 6);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(getpercentiledata_keeps_calendar_timestamps_with_non_utc_timezone)
{
	percentiledata pdata;
	struct tm start;
	time_t entry;

	ck_assert_int_eq(setenv("TZ", "Pacific/Honolulu", 1), 0);
	tzset();
	cfg.useutc = _i;
	strcpy(cfg.dbtzmodifier, cfg.useutc ? "" : DATABASELOCALTIMEMODIFIER);
	cfg.monthrotate = 10;
	cfg.monthrotatehour = 12;
	cfg.monthrotateminute = 5;
	entry = (time_t)get_timestamp(2020, 1, 10, 12, 5);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("interface"), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)(entry - 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 1, 2, (uint64_t)entry), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 2, 4, (uint64_t)(entry + 300)), 1);
	ck_assert_int_eq(getpercentiledata(&pdata, "interface", 0), 1);
	start = *localtime(&pdata.monthbegin);
	ck_assert_int_eq(start.tm_mday, 10);
	ck_assert_int_eq(start.tm_hour, 12);
	ck_assert_int_eq(start.tm_min, 5);
	ck_assert_int_eq(pdata.count, 2);
	ck_assert_int_eq(pdata.countexpectation, 2);
	ck_assert_int_eq(pdata.sumrx, 3);
	ck_assert_int_eq(pdata.sumtx, 6);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(getpercentiledata_handles_epoch_month_in_eastward_timezone)
{
	percentiledata pdata;
	dbdatalist *samples = NULL;
	dbdatalistinfo sampleinfo;
	struct tm start, sample;

	ck_assert_int_eq(setenv("TZ", "Asia/Singapore", 1), 0);
	tzset();
	cfg.useutc = _i;
	strcpy(cfg.dbtzmodifier, cfg.useutc ? "" : DATABASELOCALTIMEMODIFIER);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("interface"), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 0, 0, 85000), 1);
	ck_assert_int_eq(db_setupdated("interface", 85000), 1);
	ck_assert_int_eq(db_getdata_range(&samples, &sampleinfo, "interface", "fiveminute", 0, "", ""), 1);
	ck_assert_int_eq(sampleinfo.count, 1);
	sample = *localtime(&sampleinfo.maxtime);
	ck_assert_int_eq(sample.tm_year, 70);
	ck_assert_int_eq(sample.tm_mon, 0);
	dbdatalistfree(&samples);
	ck_assert_msg(getpercentiledata(&pdata, "interface", 0) == 1, "%s", errorstring);
	start = *localtime(&pdata.monthbegin);
	ck_assert_int_eq(start.tm_year, 70);
	ck_assert_int_eq(start.tm_mon, 0);
	ck_assert_int_eq(start.tm_mday, 1);
	ck_assert_int_eq(start.tm_hour, 0);
	ck_assert_int_eq(start.tm_min, 0);
	ck_assert_int_eq(pdata.count, 1);
	/* 各版本 SQLite 对 1970 年时区偏移换算不同，按 db_getdata_range 实际吐出的时间核对 */
	ck_assert_int_eq(pdata.databegin, sampleinfo.mintime);
	ck_assert_int_eq(pdata.dataend, sampleinfo.maxtime);
	ck_assert_int_eq(pdata.countexpectation,
		((sample.tm_mday - 1) * 1440 + sample.tm_hour * 60 + sample.tm_min) / 5 + 1);
	ck_assert_int_eq(pdata.sumrx, 0);
	ck_assert_int_eq(pdata.sumtx, 0);
	ck_assert_int_eq(pdata.rxpercentile, 0);
	ck_assert_int_eq(pdata.txpercentile, 0);
	ck_assert_int_eq(pdata.sumpercentile, 0);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

static time_t percentile_query_calendar_time(const time_t timestamp)
{
	struct tm calendar;

	if (!cfg.useutc) {
		return timestamp;
	}
	calendar = *gmtime(&timestamp);
	calendar.tm_isdst = -1;
	return mktime(&calendar);
}

START_TEST(getpercentiledata_uses_only_complete_slots_for_every_cutoff_minute)
{
	percentiledata pdata;
	time_t start, end, first, last;
	const int minute = _i % 60;

	ck_assert_int_eq(setenv("TZ", "Asia/Singapore", 1), 0);
	tzset();
	cfg.useutc = _i / 60;
	strcpy(cfg.dbtzmodifier, cfg.useutc ? "" : DATABASELOCALTIMEMODIFIER);
	cfg.monthrotate = 10;
	cfg.monthrotatehour = 23;
	cfg.monthrotateminute = minute;
	start = (time_t)get_timestamp(2020, 1, 10, 23, minute);
	end = (time_t)get_timestamp(2020, 2, 10, 23, minute);
	first = (time_t)get_timestamp(2020, 1, 10, 23, (minute + 4) / 5 * 5);
	last = (time_t)get_timestamp(2020, 2, 10, 23, minute / 5 * 5) - 300;
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("interface"), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)(first - 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 1, 2, (uint64_t)first), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 2, 4, (uint64_t)(first + 600)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 3, 6, (uint64_t)last), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)(last + 300)), 1);
	ck_assert_int_eq(db_exec("DELETE FROM month WHERE date >= '2020-02-01'"), 1);
	ck_assert_int_eq(db_setupdated("interface", end), 1);
	ck_assert_msg(getpercentiledata(&pdata, "interface", 0) == 1, "%s", errorstring);
	ck_assert_int_eq(pdata.monthbegin, percentile_query_calendar_time(start));
	ck_assert_int_eq(pdata.databegin, percentile_query_calendar_time(first));
	ck_assert_int_eq(pdata.dataend, percentile_query_calendar_time(last));
	ck_assert_int_eq(pdata.count, 3);
	ck_assert_int_eq(pdata.countexpectation, (last - first) / 300 + 1);
	ck_assert_int_eq(pdata.sumrx, 6);
	ck_assert_int_eq(pdata.sumtx, 12);
	ck_assert_int_eq(pdata.rxpercentile, 3);
	ck_assert_int_eq(pdata.txpercentile, 6);
	ck_assert_int_eq(pdata.sumpercentile, 9);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(getpercentiledata_counts_missing_slots_from_first_complete_slot)
{
	percentiledata pdata;
	time_t start, first;
	const int minute = 55 + _i % 5;

	ck_assert_int_eq(setenv("TZ", "Pacific/Honolulu", 1), 0);
	tzset();
	cfg.useutc = _i / 5;
	strcpy(cfg.dbtzmodifier, cfg.useutc ? "" : DATABASELOCALTIMEMODIFIER);
	cfg.monthrotate = 28;
	cfg.monthrotatehour = 23;
	cfg.monthrotateminute = minute;
	start = (time_t)get_timestamp(2020, 1, 28, 23, minute);
	first = (time_t)get_timestamp(2020, 1, 28, 23, (minute + 4) / 5 * 5);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("interface"), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 1, 2, (uint64_t)(first + 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 2, 4, (uint64_t)(first + 900)), 1);
	ck_assert_msg(getpercentiledata(&pdata, "interface", 0) == 1, "%s", errorstring);
	ck_assert_int_eq(pdata.monthbegin, percentile_query_calendar_time(start));
	ck_assert_int_eq(pdata.databegin, percentile_query_calendar_time(first + 300));
	ck_assert_int_eq(pdata.dataend, percentile_query_calendar_time(first + 900));
	ck_assert_int_eq(pdata.count, 2);
	ck_assert_int_eq(pdata.countexpectation, 4);
	ck_assert_int_eq(pdata.sumrx, 3);
	ck_assert_int_eq(pdata.sumtx, 6);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(getpercentiledata_returns_no_data_with_only_mixed_period_slots)
{
	const int minutes[] = {1, 2, 3, 4, 56, 57, 58, 59};
	percentiledata pdata;
	time_t start, first, endfloor;
	const int minute = minutes[_i % 8];

	ck_assert_int_eq(setenv("TZ", "Asia/Singapore", 1), 0);
	tzset();
	cfg.useutc = _i / 8;
	strcpy(cfg.dbtzmodifier, cfg.useutc ? "" : DATABASELOCALTIMEMODIFIER);
	cfg.monthrotate = 10;
	cfg.monthrotatehour = 23;
	cfg.monthrotateminute = minute;
	start = (time_t)get_timestamp(2020, 1, 10, 23, minute);
	first = (time_t)get_timestamp(2020, 1, 10, 23, (minute + 4) / 5 * 5);
	endfloor = (time_t)get_timestamp(2020, 2, 10, 23, minute / 5 * 5);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("interface"), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)(first - 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)endfloor), 1);
	ck_assert_int_eq(db_exec("DELETE FROM month"), 1);
	ck_assert_int_eq(db_exec("INSERT INTO month (interface, date, rx, tx) SELECT id, '2020-01-01', 0, 0 FROM interface"), 1);
	suppress_output();
	ck_assert_int_eq(getpercentiledata(&pdata, "interface", 0), 0);
	ck_assert_str_eq(errorstring, "No 5 minute data for 95th percentile available.");
	ck_assert_int_eq(pdata.monthbegin, percentile_query_calendar_time(start));
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

START_TEST(getpercentiledata_rounds_calendar_cutoff_across_dst_gap)
{
	percentiledata pdata;
	time_t start, first, last;

	ck_assert_int_eq(setenv("TZ", "America/New_York", 1), 0);
	tzset();
	cfg.useutc = 0;
	strcpy(cfg.dbtzmodifier, DATABASELOCALTIMEMODIFIER);
	cfg.monthrotate = 8;
	cfg.monthrotatehour = 1;
	cfg.monthrotateminute = 58;
	start = (time_t)get_timestamp(2020, 3, 8, 1, 58);
	first = (time_t)get_timestamp(2020, 3, 8, 3, 0);
	last = (time_t)get_timestamp(2020, 4, 8, 1, 50);
	ck_assert_int_eq(db_open_rw(1), 1);
	ck_assert_int_eq(db_addinterface("interface"), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)(first - 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 1, 2, (uint64_t)first), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 2, 4, (uint64_t)(first + 300)), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 3, 6, (uint64_t)last), 1);
	ck_assert_int_eq(db_addtraffic_dated("interface", 9000, 9000, (uint64_t)(last + 300)), 1);
	ck_assert_msg(getpercentiledata(&pdata, "interface", 0) == 1, "%s", errorstring);
	ck_assert_int_eq(pdata.monthbegin, start);
	ck_assert_int_eq(pdata.databegin, first);
	ck_assert_int_eq(pdata.dataend, last);
	ck_assert_int_eq(pdata.count, 3);
	ck_assert_int_eq(pdata.countexpectation, (last - first) / 300 + 1);
	ck_assert_int_eq(pdata.sumrx, 6);
	ck_assert_int_eq(pdata.sumtx, 12);
	ck_assert_int_eq(db_close(), 1);
}
END_TEST

void add_percentile_tests(Suite *s)
{
	TCase *tc_percentile = tcase_create("Percentile");
	tcase_add_checked_fixture(tc_percentile, setup, teardown);
	tcase_add_unchecked_fixture(tc_percentile, setup, teardown);
	tcase_add_test(tc_percentile, compare_uint64_t_can_compare);
	tcase_add_test(tc_percentile, compare_uint64_t_can_be_used_with_qsort);
	tcase_add_test(tc_percentile, getpercentiledata_returns_with_no_wrong_interface);
	tcase_add_test(tc_percentile, getpercentiledata_returns_with_no_data);
	tcase_add_test(tc_percentile, getpercentiledata_can_provide_data_with_one_entry);
	tcase_add_test(tc_percentile, getpercentiledata_can_provide_data_with_many_entries);
	tcase_add_test(tc_percentile, getpercentiledata_can_provide_data_with_many_entries_and_order_does_not_matter);
	tcase_add_test(tc_percentile, getpercentiledata_can_check_limit);
	tcase_add_loop_test(tc_percentile, getpercentiledata_respects_both_billing_cutoffs, 0, 4);
	tcase_add_loop_test(tc_percentile, getpercentiledata_keeps_calendar_timestamps_with_non_utc_timezone, 0, 2);
	tcase_add_loop_test(tc_percentile, getpercentiledata_handles_epoch_month_in_eastward_timezone, 0, 2);
	tcase_add_loop_test(tc_percentile, getpercentiledata_uses_only_complete_slots_for_every_cutoff_minute, 0, 120);
	tcase_add_loop_test(tc_percentile, getpercentiledata_counts_missing_slots_from_first_complete_slot, 0, 10);
	tcase_add_loop_test(tc_percentile, getpercentiledata_returns_no_data_with_only_mixed_period_slots, 0, 16);
	tcase_add_test(tc_percentile, getpercentiledata_rounds_calendar_cutoff_across_dst_gap);
	suite_add_tcase(s, tc_percentile);
}
