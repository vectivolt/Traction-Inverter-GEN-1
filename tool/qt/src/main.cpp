// Traction Tool — entry point. Command line:
//   --sim [--sku 8xx_sic|8xx_igbt|4xx_igbt|4xx_sic] [--rate HZ] [--time FACTOR]   start the simulator bridge
//   --replay FILE                                                                play a recorded log
//   --light                                                                      light theme
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Traction Tool"));
    QApplication::setOrganizationName(QStringLiteral("JoulePoint"));
    QApplication::setOrganizationDomain(QStringLiteral("joulepoint.com"));
    QApplication::setApplicationVersion(QStringLiteral(TT_VERSION));
    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Bench and service tool for the 220 kW / 800 V traction inverter"));
    cli.addHelpOption();
    cli.addVersionOption();
    const QCommandLineOption sim(QStringLiteral("sim"), QStringLiteral("Start the simulator bridge."));
    const QCommandLineOption sku(QStringLiteral("sku"), QStringLiteral("Simulator SKU."), QStringLiteral("sku"), QStringLiteral("8xx_sic"));
    const QCommandLineOption rate(QStringLiteral("rate"), QStringLiteral("Telemetry rate (Hz)."), QStringLiteral("hz"), QStringLiteral("100"));
    const QCommandLineOption time(QStringLiteral("time"), QStringLiteral("Time factor."), QStringLiteral("factor"), QStringLiteral("1"));
    const QCommandLineOption replay(QStringLiteral("replay"), QStringLiteral("Play a recorded log."), QStringLiteral("file"));
    const QCommandLineOption light(QStringLiteral("light"), QStringLiteral("Light theme."));
    cli.addOptions({sim, sku, rate, time, replay, light});
    cli.process(app);

    const bool lightTheme = cli.isSet(light) || QSettings().value(QStringLiteral("ui/light"), false).toBool();
    Theme::apply(app, lightTheme ? Theme::Variant::Light : Theme::Variant::Dark);
    MainWindow w;
    w.show();
    if (cli.isSet(sim)) {
        BridgeTransport::Options o;
        o.program = BridgeTransport::locate();
        o.sku = cli.value(sku);
        o.rateHz = cli.value(rate).toDouble();
        o.timeFactor = cli.value(time).toDouble();
        w.connectSimulator(o);
    } else if (cli.isSet(replay)) {
        w.openReplay(cli.value(replay));
    }
    return QApplication::exec();
}
