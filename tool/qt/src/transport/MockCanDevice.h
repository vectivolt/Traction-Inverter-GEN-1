// MockCanDevice — a QCanBusDevice with no hardware: frames written are kept (written()), frames "received" are
// injected by the test (inject()). Connects at once.
#pragma once

#include <QCanBusDevice>
#include <QVector>

class MockCanDevice : public QCanBusDevice
{
    Q_OBJECT
public:
    explicit MockCanDevice(QObject *parent = nullptr) : QCanBusDevice(parent) {}

    bool writeFrame(const QCanBusFrame &frame) override
    {
        if (state() != ConnectedState || !frame.isValid()) {
            return false;
        }
        m_written.push_back(frame);
        Q_EMIT framesWritten(1);
        return true;
    }
    QString interpretErrorFrame(const QCanBusFrame &) override { return QStringLiteral("mock error frame"); }

    void inject(const QVector<QCanBusFrame> &frames) { enqueueReceivedFrames(frames); }
    const QVector<QCanBusFrame> &written() const { return m_written; }
    void clearWritten() { m_written.clear(); }

protected:
    bool open() override
    {
        setState(ConnectedState);
        return true;
    }
    void close() override { setState(UnconnectedState); }

private:
    QVector<QCanBusFrame> m_written;
};
