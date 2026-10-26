#include "controller.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(SERVICE_VERSION, configFile, true), m_timer(new QTimer(this)), m_status(false), m_names(false)
{
    QFile file(getConfig()->value("snmp/custom", "/opt/homed-custom-snmp/custom.json").toString());
    QList <QString> names = getConfig()->childGroups();
    QString instance = serviceTopic().split('/').value(1);
    QProcess process;
    QJsonArray custom;

    m_topic = instance.isEmpty() ? "custom" : QString("custom/%1").arg(instance);
    process.start("sh", {"-c", "command -v snmpget snmpwalk"});
    process.waitForFinished();

    if (process.exitCode())
        return;

    if (file.open(QFile::ReadOnly))
    {
        custom = QJsonDocument::fromJson(file.readAll()).array();
        file.close();
    }

    for (int i = 0; i < names.count(); i++)
    {
        const QString &name = names.at(i);

        if (name != "log" && name != "mqtt" && name != "snmp")
        {
            QString address = getConfig()->value(QString("%1/address").arg(name)).toString(), community = getConfig()->value(QString("%1/community").arg(name), "public").toString(), disks = getConfig()->value(QString("%1/disks").arg(name)).toStringList().join(','), ports = getConfig()->value(QString("%1/ports").arg(name)).toStringList().join(',');
            quint32 interval = getConfig()->value(QString("%1/interval").arg(name), DEFAULT_INTERVAL).toInt();
            bool debug = getConfig()->value(QString("%1/debug").arg(name), false).toBool();
            QMap <QString, QVariant> items, options;
            Device device;

            if (address.isEmpty())
                continue;

            for (auto it = custom.begin(); it != custom.end(); it++)
            {
                QJsonObject json = it->toObject();

                if (!json.value("devices").toArray().contains(name))
                    continue;

                items.insert(json.value("items").toObject().toVariantMap());
                options.insert(json.value("options").toObject().toVariantMap());
            }

            device = Device(new DeviceObject(address, community, disks, ports, items, options, interval, QString("snmp-%1").arg(name), debug));

            connect(device.data(), &DeviceObject::deviceUpdated, this, &Controller::deviceUpdated);
            connect(device.data(), &DeviceObject::availabilityUpdated, this, &Controller::availabilityUpdated);
            connect(device.data(), &DeviceObject::propertiesUpdated, this, &Controller::propertiesUpdated);
            connect(device.data(), &DeviceObject::pollFinished, this, &Controller::update);

            m_devices.append(device);
        }
    }

    connect(m_timer, &QTimer::timeout, this, &Controller::update);

    m_timer->setSingleShot(true);
    m_timer->start(0);
}

void Controller::publishDevice(DeviceObject *device)
{
    QJsonObject data = device->data().isEmpty() ? QJsonObject {{"active", true}, {"cloud", false}, {"discovery", false}} : device->data();

    data.insert("id", device->id());
    data.insert("service", QCoreApplication::applicationName());
    data.insert("exposes", device->exposes());
    data.insert("options", device->options());
    data.insert("real", true);

    mqttPublish(mqttTopic("command/%1").arg(m_topic), {{"action", "updateDevice"}, {"device", device->id()}, {"data", data}});
    device->setPublished(true);
}

void Controller::publishAvailability(DeviceObject *device)
{
    mqttPublish(mqttTopic("device/%1/%2").arg(m_topic, m_names ? device->name() : device->id()), {{"status", device->availability() == Availability::Online ? "online" : "offline"}}, true);
}

void Controller::quit(void)
{
    for (int i = 0; i < m_devices.count(); i++)
        mqttPublish(mqttTopic("device/%1/%2").arg(m_topic, m_names ? m_devices.at(i)->name() : m_devices.at(i)->id()), {{"status", "offline"}}, true);

    HOMEd::quit();
}

void Controller::mqttConnected(void)
{
    mqttSubscribe(mqttTopic("service/%1").arg(m_topic));
    mqttPublishService();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(0, mqttTopic().length(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (subTopic == QString("service/%1").arg(m_topic))
    {
        if (json.value("status").toString() != "online")
        {
            m_status = false;

            for (int i = 0; i < m_devices.count(); i++)
            {
                mqttUnsubscribe(mqttTopic("td/%1/%2").arg(m_topic, m_names ? m_devices.at(i)->name() : m_devices.at(i)->id()));
                m_devices.at(i)->setPublished(false);
            }

            return;
        }

        mqttSubscribe(mqttTopic("status/%1").arg(m_topic));
    }
    else if (subTopic == QString("status/%1").arg(m_topic))
    {
        QList <QString> list = {"id", "service", "exposes", "options", "real"};
        QJsonArray devices = json.value("devices").toArray();

        m_status = true;
        m_names = json.value("names").toBool();

        for (int i = 0; i < m_devices.count(); i++)
        {
            const Device &device = m_devices.at(i);

            for (auto it = devices.begin(); it != devices.end(); it++)
            {
                QJsonObject item = it->toObject();

                if (item.value("id").toString() != device->id())
                    continue;

                for (int j = 0; j < list.count(); j++)
                    item.remove(list.at(j));

                if (m_names && item.value("name").toString(device->id()) != device->name())
                {
                    mqttPublish(mqttTopic("device/%1/%2").arg(m_topic, device->name()), QJsonObject(), true);
                    mqttUnsubscribe(mqttTopic("td/%1/%2").arg(m_topic, device->name()));
                }

                device->setData(item);
                break;
            }

            mqttSubscribe(mqttTopic("td/%1/%2").arg(m_topic, m_names ? device->name() : device->id()));

            if (device->ready() && !device->published())
                publishDevice(device.data());

            publishAvailability(device.data());
        }

        for (auto it = devices.begin(); it != devices.end(); it++)
        {
            QJsonObject item = it->toObject();
            QString id = item.value("id").toString();
            bool check = true;

            if (item.value("service").toString() != QCoreApplication::applicationName())
                continue;

            for (int i = 0; i < m_devices.count(); i++)
            {
                if (m_devices.at(i)->id() != id)
                    continue;

                check = false;
                break;
            }

            if (!check)
                continue;

            mqttPublish(mqttTopic("command/%1").arg(m_topic), QJsonObject {{"action", "removeDevice"}, {"device", id}});
        }
    }
}

void Controller::deviceUpdated(void)
{
    DeviceObject *device = reinterpret_cast <DeviceObject*> (sender());

    if (!m_status || device->published())
        return;

    publishDevice(device);
}

void Controller::availabilityUpdated(void)
{
    DeviceObject *device = reinterpret_cast <DeviceObject*> (sender());

    logInfo << device << "is" << (device->availability() == Availability::Online ? "online" : "offline");

    if (!m_status)
        return;

    publishAvailability(device);
}

void Controller::propertiesUpdated(void)
{
    DeviceObject *device = reinterpret_cast <DeviceObject*> (sender());
    mqttPublish(mqttTopic("fd/%1/%2").arg(m_topic, m_names ? device->name() : device->id()), QJsonObject::fromVariantMap(device->properties()));
}

void Controller::update(void)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch(), nextPoll = -1;

    for (int i = 0; i < m_devices.count(); i++)
    {
        const Device &device = m_devices.at(i);
        qint64 time = device->pollTime() + device->pollInterval();

        if (device->polling())
            continue;

        if (time <= now)
        {
            device->poll();
            continue;
        }

        if (nextPoll >= 0 && nextPoll <= time)
            continue;

        nextPoll = time;
    }

    if (nextPoll < 0)
        return;

    m_timer->start(nextPoll - now);
}
