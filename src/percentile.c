#include "common.h"
#include "dbsql.h"
#include "percentile.h"

static time_t percentilegridcutoff(const time_t cutoff, const int roundup)
{
	struct tm calendar;
	int remainder;

	if ((cfg.useutc ? gmtime_r(&cutoff, &calendar) : localtime_r(&cutoff, &calendar)) == NULL) {
		return (time_t)-1;
	}
	if (calendar.tm_min % 5 == 0 && calendar.tm_sec == 0) {
		/* 已经在 5 分钟整点上就直接返回，免得夏令时重叠时刻被 mktime 算到另一边 */
		return cutoff;
	}
	if (roundup && calendar.tm_sec != 0) {
		calendar.tm_min++;
	}
	remainder = calendar.tm_min % 5;
	calendar.tm_min += roundup ? (5 - remainder) % 5 : -remainder;
	calendar.tm_sec = 0;
	calendar.tm_isdst = -1;
	return cfg.useutc ? timegm(&calendar) : mktime(&calendar);
}

int getpercentiledata(percentiledata *pdata, const char *iface, const uint64_t userlimitbytespersecond)
{
	uint32_t entry = 0, entrylimit;
	uint64_t *rxdata, *txdata, *sumdata, slotlimit;
	const struct tm *d;
	struct tm calendar;
	time_t monthlabel, monthbegin, monthend, firstslot, lastslot;
	char datebuff[DATEBUFFLEN], dateend[DATEBUFFLEN];
	dbdatalist *datalist = NULL, *datalist_i = NULL;
	dbdatalistinfo datainfo;

	pdata->userlimitbytespersecond = userlimitbytespersecond;
	pdata->countrxoveruserlimit = 0;
	pdata->counttxoveruserlimit = 0;
	pdata->countsumoveruserlimit = 0;

	if (!db_getdata_range(&datalist, &datainfo, iface, "month", 1, "", "")) {
		snprintf(errorstring, 1024, "Failed to fetch month data for 95th percentile.");
		printe(PT_Error);
		return 0;
	}

	if (!datalist) {
		snprintf(errorstring, 1024, "No month data for 95th percentile available.");
		printe(PT_Error);
		return 0;
	}

	monthlabel = db_getmonthlabel(datalist->rowid);
	monthbegin = monthlabel == (time_t)-1 ? (time_t)-1 : billingperiodstart(monthlabel, 0, 0);
	monthend = monthlabel == (time_t)-1 ? (time_t)-1 : billingperiodstart(monthlabel, 0, 1);
	dbdatalistfree(&datalist);

	if (monthbegin == (time_t)-1 || monthend == (time_t)-1 || monthend <= monthbegin) {
		snprintf(errorstring, 1024, "Failed to determine billing period for 95th percentile.");
		printe(PT_Error);
		return 0;
	}
	pdata->monthbegin = monthbegin;
	if (cfg.useutc) {
		/* 对外返回遵循查询日历口径，和其余接口保持一致 */
		if (gmtime_r(&monthbegin, &calendar) == NULL) {
			snprintf(errorstring, 1024, "Failed to determine billing period for 95th percentile.");
			printe(PT_Error);
			return 0;
		}
		calendar.tm_isdst = -1;
		pdata->monthbegin = mktime(&calendar);
	}
	/* 落盘的 5 分钟记录无法再拆开，跨账期边界的那个格子两边都不纳入 95 计费统计 */
	firstslot = percentilegridcutoff(monthbegin, 1);
	lastslot = percentilegridcutoff(monthend, 0);
	if (firstslot == (time_t)-1 || lastslot == (time_t)-1) {
		snprintf(errorstring, 1024, "Failed to determine billing period for 95th percentile.");
		printe(PT_Error);
		return 0;
	}
	/* db_getdata_range 结束时间是闭区间且库里存的是槽起点，这里往前退 5 分钟 */
	lastslot -= 300;
	if (lastslot < firstslot) {
		snprintf(errorstring, 1024, "No 5 minute data for 95th percentile available.");
		printe(PT_Error);
		return 0;
	}
	d = cfg.useutc ? gmtime(&firstslot) : localtime(&firstslot);
	strftime(datebuff, DATEBUFFLEN, "%Y-%m-%d %H:%M", d);
	d = cfg.useutc ? gmtime(&lastslot) : localtime(&lastslot);
	strftime(dateend, DATEBUFFLEN, "%Y-%m-%d %H:%M", d);

	if (!db_getdata_range(&datalist, &datainfo, iface, "fiveminute", 0, datebuff, dateend)) {
		snprintf(errorstring, 1024, "Failed to fetch 5 minute data for 95th percentile.");
		printe(PT_Error);
		return 0;
	}

	if (!datalist) {
		snprintf(errorstring, 1024, "No 5 minute data for 95th percentile available.");
		printe(PT_Error);
		return 0;
	}

	pdata->databegin = datainfo.mintime;
	pdata->dataend = datainfo.maxtime;
	pdata->count = datainfo.count;
	pdata->countexpectation = (uint32_t)((billingcalendartime(pdata->dataend) - firstslot) / 300 + 1);
	pdata->minrx = datainfo.minrx;
	pdata->mintx = datainfo.mintx;
	pdata->min = datainfo.min;
	pdata->sumrx = datainfo.sumrx;
	pdata->sumtx = datainfo.sumtx;
	pdata->maxrx = datainfo.maxrx;
	pdata->maxtx = datainfo.maxtx;
	pdata->max = datainfo.max;

	entrylimit = (uint32_t)lrint(datainfo.count * (double)0.95) - 1;

	if (debug) {
		printf("Entry expectation: %" PRIu32 "\n", pdata->countexpectation);
		printf("Entry count: %" PRIu32 "\n", pdata->count);
		printf("95th: %" PRIu32 "\n", entrylimit);
	}

	rxdata = (uint64_t *)malloc(datainfo.count * sizeof(uint64_t));
	txdata = (uint64_t *)malloc(datainfo.count * sizeof(uint64_t));
	sumdata = (uint64_t *)malloc(datainfo.count * sizeof(uint64_t));
	if (rxdata == NULL || txdata == NULL || sumdata == NULL) {
		panicexit(__FILE__, __LINE__);
	}

	slotlimit = (userlimitbytespersecond > UINT64_MAX / 300) ? UINT64_MAX : userlimitbytespersecond * 300;
	datalist_i = datalist;
	while (datalist_i != NULL) {
		if (entry >= datainfo.count) {
			snprintf(errorstring, 1024, "Database query resulted in more data than expected (%" PRIu32 " >= %" PRIu32 ").", entry, datainfo.count);
			printe(PT_Error);
			free(rxdata);
			free(txdata);
			free(sumdata);
			dbdatalistfree(&datalist);
			return 0;
		}
		rxdata[entry] = datalist_i->rx;
		txdata[entry] = datalist_i->tx;
		sumdata[entry] = datalist_i->rx + datalist_i->tx;

		if (userlimitbytespersecond > 0) {
			if (rxdata[entry] > slotlimit) {
				pdata->countrxoveruserlimit++;
			}
			if (txdata[entry] > slotlimit) {
				pdata->counttxoveruserlimit++;
			}
			if (sumdata[entry] > slotlimit) {
				pdata->countsumoveruserlimit++;
			}
		}

		datalist_i = datalist_i->next;
		entry++;
	}

	if (datainfo.count != entry) {
		snprintf(errorstring, 1024, "Database query data count doesn't match entry count (%" PRIu32 " != %" PRIu32 ").", datainfo.count, entry);
		printe(PT_Error);
		free(rxdata);
		free(txdata);
		free(sumdata);
		dbdatalistfree(&datalist);
		return 0;
	}

	qsort((void *)rxdata, datainfo.count, sizeof(uint64_t), compare_uint64_t);
	qsort((void *)txdata, datainfo.count, sizeof(uint64_t), compare_uint64_t);
	qsort((void *)sumdata, datainfo.count, sizeof(uint64_t), compare_uint64_t);

	pdata->rxpercentile = rxdata[entrylimit];
	pdata->txpercentile = txdata[entrylimit];
	pdata->sumpercentile = sumdata[entrylimit];

	free(rxdata);
	free(txdata);
	free(sumdata);
	dbdatalistfree(&datalist);

	return 1;
}

int compare_uint64_t(const void *a, const void *b)
{
	if (*(const uint64_t *)a < *(const uint64_t *)b) {
		return -1;
	} else if (*(const uint64_t *)a > *(const uint64_t *)b) {
		return 1;
	}
	return 0;
}
