/*
 * Copyright (c) 2009 Sean C. Rhea (srhea@srhea.net)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "RideSummaryWindow.h"
#include "MainWindow.h"
#include "RideFile.h"
#include "RideItem.h"
#include "RideMetric.h"
#include "Settings.h"
#include "TimeUtils.h"
#include "Units.h"
#include "Zones.h"
#include <QtGui>
#include <QtXml/QtXml>
#include <assert.h>
#include <math.h>

RideSummaryWindow::RideSummaryWindow(MainWindow *mainWindow) :
    QWidget(mainWindow), mainWindow(mainWindow)
{
    QVBoxLayout *vlayout = new QVBoxLayout;
    rideSummary = new QTextEdit(this);
    rideSummary->setReadOnly(true);
    vlayout->addWidget(rideSummary);

    connect(mainWindow, SIGNAL(rideSelected()), this, SLOT(refresh()));
    connect(mainWindow, SIGNAL(zonesChanged()), this, SLOT(refresh()));
    connect(mainWindow, SIGNAL(intervalsChanged()), this, SLOT(refresh()));
    setLayout(vlayout);
}

void
RideSummaryWindow::refresh()
{
    if (!mainWindow->rideItem()) {
	rideSummary->clear();
        return;
    }
    rideSummary->setHtml(htmlSummary());
    rideSummary->setAlignment(Qt::AlignCenter);
}

static void
summarize(bool even,
          QString &intervals,
          const QString &name,
          double km_start, double km_end,
          double &int_watts_sum,
          double &int_hr_sum,
          QVector<double> &int_hrs,
          double &int_cad_sum,
          double &int_kph_sum,
          double &int_secs_hr,
          double &int_max_power,
          double dur)
{
    double mile_len = (km_end - km_start) * MILES_PER_KM;
    double minutes = (int) (dur/60.0);
    double seconds = dur - (60 * minutes);
    double watts_avg = int_watts_sum / dur;
    double hr_avg = int_secs_hr > 0.0 ? int_hr_sum / int_secs_hr : 0.0;
    double cad_avg = int_cad_sum / dur;
    double mph_avg = int_kph_sum * MILES_PER_KM / dur;
    double energy = int_watts_sum / 1000.0; // watts_avg / 1000.0 * dur;
    std::sort(int_hrs.begin(), int_hrs.end());
    double top5hr = int_hrs.size() > 0 ? int_hrs[int_hrs.size() * 0.95] : 0;

    if (even)
        intervals += "<tr><td align=\"center\">%1</td>";
    else {
        QColor color = QApplication::palette().alternateBase().color();
        color = QColor::fromHsv(color.hue(), color.saturation() * 2, color.value());
        intervals += "<tr bgcolor='" + color.name() + "'><td align=\"center\">%1</td>";
    }

    boost::shared_ptr<QSettings> settings = GetApplicationSettings();
    QVariant unit = settings->value(GC_UNIT);

    intervals += "<td align=\"center\">%2:%3</td>";
    intervals += "<td align=\"center\">%4</td>";
    intervals += "<td align=\"center\">%5</td>";
    intervals += "<td align=\"center\">%6</td>";
    intervals += "<td align=\"center\">%7</td>";
    intervals += "<td align=\"center\">%8</td>";
    intervals += "<td align=\"center\">%9</td>";
    intervals += "<td align=\"center\">%10</td>";
    intervals += "<td align=\"center\">%11</td>";
    intervals = intervals.arg(name);
    intervals = intervals.arg(minutes, 0, 'f', 0);
    intervals = intervals.arg(seconds, 2, 'f', 0, QLatin1Char('0'));
    if(unit.toString() == "Metric")
        intervals = intervals.arg(mile_len * KM_PER_MILE, 0, 'f', 1);
    else
        intervals = intervals.arg(mile_len, 0, 'f', 1);
    intervals = intervals.arg(energy, 0, 'f', 0);
    intervals = intervals.arg(int_max_power, 0, 'f', 0);
    intervals = intervals.arg(watts_avg, 0, 'f', 0);
    intervals = intervals.arg(top5hr, 0, 'f', 0);
    intervals = intervals.arg(hr_avg, 0, 'f', 0);
    intervals = intervals.arg(cad_avg, 0, 'f', 0);


    if(unit.toString() == "Metric")
        intervals = intervals.arg(mph_avg * 1.60934, 0, 'f', 1);
    else
        intervals = intervals.arg(mph_avg, 0, 'f', 1);
}

QString
RideSummaryWindow::htmlSummary() const
{
    QString summary;

    RideItem *rideItem = mainWindow->rideItem();
    RideFile *ride = rideItem->ride();

    // ridefile read errors?
    if (!ride) {
        summary = tr("<p>Couldn't read file \"");
        summary += rideItem->fileName + "\":";
        QListIterator<QString> i(mainWindow->rideItem()->errors());
        while (i.hasNext())
            summary += "<br>" + i.next();
        return summary;
    }

    summary = ("<p><center><h2>"
               + rideItem->dateTime.toString(tr("dddd MMMM d, yyyy, h:mm AP"))
               + "</h2><h3>" + tr("Device Type: ") + ride->deviceType() + "</h3>");

    rideItem->computeMetrics();

    boost::shared_ptr<QSettings> settings = GetApplicationSettings();
    QVariant unit = settings->value(GC_UNIT);

    QString intervals = "";
    double secs_delta = ride->recIntSecs();

    bool even = false;
    foreach (RideFileInterval interval, ride->intervals()) {
        int i = ride->intervalBegin(interval);
        assert(i < ride->dataPoints().size());
        const RideFilePoint *firstPoint = ride->dataPoints()[i];

        double secs_watts = 0.0;
        double int_watts_sum = 0.0;
        double int_hr_sum = 0.0;
        QVector<double> int_hrs;
        double int_cad_sum = 0.0;
        double int_kph_sum = 0.0;
        double int_secs_hr = 0.0;
        double int_max_power = 0.0;
        double km_end = 0.0;

        while (i < ride->dataPoints().size()) {
            const RideFilePoint *point = ride->dataPoints()[i++];
            if (point->secs >= interval.stop)
                break;
            if (point->watts >= 0.0) {
                secs_watts += secs_delta;
                int_watts_sum += point->watts * secs_delta;
                if (point->watts > int_max_power)
                    int_max_power = point->watts;
            }
            if (point->hr > 0) {
                int_hr_sum += point->hr * secs_delta;
                int_secs_hr += secs_delta;
            }
            if (point->hr >= 0)
                int_hrs.push_back(point->hr);
            if (point->cad > 0)
                int_cad_sum += point->cad * secs_delta;
            if (point->kph >= 0)
                int_kph_sum += point->kph * secs_delta;

            km_end = point->km;
        }
        summarize(even, intervals, interval.name,
                  firstPoint->km, km_end, int_watts_sum,
                  int_hr_sum, int_hrs, int_cad_sum, int_kph_sum,
                  int_secs_hr, int_max_power,
                  interval.stop - interval.start);
        even = !even;
    }

    summary += "<p>";

    bool metricUnits = (unit.toString() == "Metric");

    const int columns = 3;
    const char *columnNames[] = { "Totals", "Averages", "Metrics*" };
    const char *totalColumn[] = {
        "workout_time",
        "time_riding",
        "total_distance",
        "total_work",
        "elevation_gain",
        NULL
    };

    const char *averageColumn[] = {
        "average_speed",
        "average_power",
        "average_hr",
        "average_cad",
        NULL
    };

    const char *metricColumn[] = {
        "skiba_xpower",
        "skiba_relative_intensity",
        "skiba_bike_score",
        "daniels_points",
        "aerobic_decoupling",
        NULL
    };

    summary += "<table border=0 cellspacing=10><tr>";
    for (int i = 0; i < columns; ++i) {
        summary += "<td align=\"center\" width=\"%1%\"><table>"
            "<tr><td align=\"center\" colspan=2><h2>%2</h2></td></tr>";
        summary = summary.arg(90 / columns);
        summary = summary.arg(columnNames[i]);
        const char **metricsList;
        switch (i) {
            case 0: metricsList = totalColumn; break;
            case 1: metricsList = averageColumn; break;
            case 2: metricsList = metricColumn; break;
            default: assert(false);
        }
        for (int j = 0;; ++j) {
            const char *symbol = metricsList[j];
            if (!symbol) break;
            RideMetricPtr m = rideItem->metrics.value(symbol);
            QString name = m->name().replace(QRegExp(tr("^Average ")), "");
            if (m->units(metricUnits) == "seconds") {
                QString s("<tr><td>%1:</td><td "
                          "align=\"right\">%2</td></tr>");
                s = s.arg(name);
                s = s.arg(time_to_string(m->value(metricUnits)));
                summary += s;
            }
            else {
                QString s = "<tr><td>" + name;
                if (m->units(metricUnits) != "")
                    s += " (" + m->units(metricUnits) + ")";
                s += ":</td><td align=\"right\">%1</td></tr>";
                if (m->precision() == 0)
                    s = s.arg((unsigned) round(m->value(metricUnits)));
                else
                    s = s.arg(m->value(metricUnits), 0, 'f', m->precision());
                summary += s;
            }
        }
        summary += "</table></td>";
    }
    summary += "</tr></table>";

    if (rideItem->numZones() > 0) {
        QVector<double> time_in_zone(rideItem->numZones());
        for (int i = 0; i < rideItem->numZones(); ++i)
            time_in_zone[i] = rideItem->timeInZone(i);
        summary += tr("<h2>Power Zones</h2>");
        summary += mainWindow->zones()->summarize(rideItem->zoneRange(), time_in_zone);
    }

    if (ride->intervals().size() > 0) {
        bool firstRow = true;
        QStringList intervalMetrics;
        intervalMetrics << "workout_time";
        intervalMetrics << "total_distance";
        intervalMetrics << "total_work";
        intervalMetrics << "average_power";
        intervalMetrics << "skiba_xpower";
        intervalMetrics << "max_power";
        intervalMetrics << "average_hr";
        intervalMetrics << "ninety_five_percent_hr";
        intervalMetrics << "average_cad";
        intervalMetrics << "average_speed";
        summary += "<p><h2>Intervals</h2>\n<p>\n";
        summary += "<table align=\"center\" width=\"90%\" ";
        summary += "cellspacing=0 border=0>";
        bool even = false;
        foreach (RideFileInterval interval, ride->intervals()) {
            RideFile f(ride->startTime(), ride->recIntSecs());
            for (int i = ride->intervalBegin(interval); i < ride->dataPoints().size(); ++i) {
                const RideFilePoint *p = ride->dataPoints()[i];
                if (p->secs >= interval.stop)
                    break;
                f.appendPoint(p->secs, p->cad, p->hr, p->km, p->kph, p->nm,
                              p->watts, p->alt, p->lon, p->lat, 0);
            }
            QHash<QString,RideMetricPtr> metrics =
                RideMetric::computeMetrics(&f, mainWindow->zones(), intervalMetrics);
            if (firstRow) {
                summary += "<tr>";
                summary += "<td align=\"center\" valign=\"bottom\">Interval Name</td>";
                foreach (QString symbol, intervalMetrics) {
                    RideMetricPtr m = metrics.value(symbol);
                    summary += "<td align=\"center\" valign=\"bottom\">" + m->name();
                    if (m->units(metricUnits) == "seconds")
                        ; // don't do anything
                    else if (m->units(metricUnits).size() > 0)
                        summary += " (" + m->units(metricUnits) + ")";
                    summary += "</td>";
                }
                summary += "</tr>";
                firstRow = false;
            }
            if (even)
                summary += "<tr>";
            else {
                QColor color = QApplication::palette().alternateBase().color();
                color = QColor::fromHsv(color.hue(), color.saturation() * 2, color.value());
                summary += "<tr bgcolor='" + color.name() + "'>";
            }
            even = !even;
            summary += "<td align=\"center\">" + interval.name + "</td>";
            foreach (QString symbol, intervalMetrics) {
                RideMetricPtr m = metrics.value(symbol);
                QString s("<td align=\"center\">%1</td>");
                if (m->units(metricUnits) == "seconds")
                    summary += s.arg(time_to_string(m->value(metricUnits)));
                else
                    summary += s.arg(m->value(metricUnits), 0, 'f', m->precision());
            }
            summary += "</tr>";
        }
        summary += "</table>";
    }


    // TODO: Ergomo uses non-consecutive interval numbers.
    // Seems to use 0 when not in an interval
    // and an integer < 30 when in an interval.
    // We'll need to create a counter for the intervals
    // rather than relying on the final data point's interval number.
    if (ride->intervals().size() > 0) {
        summary += "<p><h2>Intervals</h2>\n<p>\n";
        summary += "<table align=\"center\" width=\"90%\" ";
        summary += "cellspacing=0 border=0><tr>";
        summary += "<td align=\"center\">Interval</td>";
        summary += "<td align=\"center\"></td>";
        summary += tr("<td align=\"center\">Distance</td>");
        summary += tr("<td align=\"center\">Work</td>");
        summary += tr("<td align=\"center\">Max Power</td>");
        summary += tr("<td align=\"center\">Avg Power</td>");
        summary += tr("<td align=\"center\">95% HR</td>");
        summary += tr("<td align=\"center\">Avg HR</td>");
        summary += tr("<td align=\"center\">Avg Cadence</td>");
        summary += tr("<td align=\"center\">Avg Speed</td>");
        summary += "</tr><tr>";
        summary += tr("<td align=\"center\">Number</td>");
        summary += tr("<td align=\"center\">Duration</td>");
        if(unit.toString() == "Metric")
            summary += "<td align=\"center\">(km)</td>";
        else
            summary += "<td align=\"center\">(miles)</td>";
        summary += "<td align=\"center\">(kJ)</td>";
        summary += "<td align=\"center\">(watts)</td>";
        summary += "<td align=\"center\">(watts)</td>";
        summary += "<td align=\"center\">(bpm)</td>";
        summary += "<td align=\"center\">(bpm)</td>";
        summary += "<td align=\"center\">(rpm)</td>";
        if(unit.toString() == "Metric")
            summary += "<td align=\"center\">(km/h)</td>";
        else
            summary += "<td align=\"center\">(mph)</td>";
        summary += "</tr>";
        summary += intervals;
        summary += "</table>";
    }

    if (!rideItem->errors().empty()) {
        summary += tr("<p><h2>Errors reading file:</h2><ul>");
        QStringListIterator i(rideItem->errors());
        while(i.hasNext())
            summary += " <li>" + i.next();
        summary += "</ul>";
    }
    summary += "<br><hr width=\"80%\"></center>";

    // The extra <center> works around a bug in QT 4.3.1,
    // which will otherwise put the following above the <hr>.
    summary += "<center>BikeScore is a trademark of Dr. Philip "
        "Friere Skiba, PhysFarm Training Systems LLC</center>";

    return summary;
}

