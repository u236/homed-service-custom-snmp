#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION     "1.0.0"
#define DEFAULT_INTERVAL    5000

#include "device.h"
#include "homed.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

private:

    QTimer *m_timer;

    QString m_topic;
    bool m_status, m_names;

    QList <Device> m_devices;

    void publishDevice(DeviceObject *device);
    void publishAvailability(DeviceObject *device);

public slots:

    void quit(void) override;

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void deviceUpdated(void);
    void availabilityUpdated(void);
    void propertiesUpdated(void);

    void update(void);

};

#endif
