#include "device.h"
#include "logger.h"
#include "parser.h"

QJsonObject DeviceObject::options(void)
{
    QJsonObject options, list =
    {
        {"diskName",    QJsonObject {{"type", "sensor"}, {"icon", "mdi:tag-outline"}}},
        {"diskUsage",   QJsonObject {{"type", "sensor"}, {"unit", "%"}, {"icon", "mdi:floppy"}}},
        {"portName",    QJsonObject {{"type", "sensor"}, {"icon", "mdi:tag-outline"}}},
        {"portLink",    QJsonObject {{"type", "binary"}, {"icon", "mdi:link-variant"}}},
        {"portSpeed",   QJsonObject {{"type", "sensor"}, {"unit", "Mbit/s"}, {"icon", "mdi:speedometer"}}},
        {"cpuLoad",     QJsonObject {{"type", "sensor"}, {"unit", "%"}, {"icon", "mdi:poll"}}},
        {"memoryUsage", QJsonObject {{"type", "sensor"}, {"unit", "%"}, {"icon", "mdi:chip"}}},
        {"uptime",      QJsonObject {{"type", "sensor"}, {"unit", "sec"}, {"icon", "mdi:timer"}}}
    };

    for (auto it = m_exposes.begin(); it != m_exposes.end(); it++)
    {
        QString expose = it->toString(), item = expose.split('_').value(0);

        if (list.contains(item))
            options.insert(item, list.value(item));

        if (m_options.contains(item))
            options.insert(item, QJsonValue::fromVariant(m_options.value(item)));

        if (m_options.contains(expose))
            options.insert(expose, QJsonValue::fromVariant(m_options.value(expose)));
    }

    for (auto it = options.begin(); it != options.end(); it++)
    {
        QJsonObject option = it.value().toObject();

        if (!option.contains("divider"))
            continue;

        option.remove("divider");
        it.value() = option;
    }

    return options;
}

void DeviceObject::poll(void)
{
    m_index = 0;
    m_pollTime = QDateTime::currentMSecsSinceEpoch();
    m_polling = true;

    if (m_discovery)
    {
        m_list.clear();

        if (m_disks == "all")
            m_list.append(OID_HR_FS_ENTRY);

        if (m_ports == "all")
            m_list.append(OID_IF_TYPE);

        if (!m_ports.isEmpty())
            m_list.append(OID_IF_NAME);

        m_list.append(OID_HR_STORAGE_ENTRY);
        m_list.append(OID_HR_PROCESSOR_LOAD);
    }

    m_values.clear();
    sendRequest();
}

void DeviceObject::updateAvailability(Availability availability)
{
    if (m_availability == availability)
        return;

    m_availability = availability;
    emit availabilityUpdated();
}

void DeviceObject::sendRequest(void)
{
    QString request = QString("%1 -v2c -c '%2' -m '' -t 1 -r 2 -Oenqt %3 %4").arg(m_discovery ? "snmpwalk" : "snmpget", QString(m_community).replace("'", "'\\''"), m_address, m_list.mid(m_index, m_discovery ? 1 : REQUEST_MAX_ITEMS).join(0x20));
    QProcess *process(new QProcess(this));

    connect(process, static_cast <void (QProcess::*)(int, QProcess::ExitStatus)> (&QProcess::finished), this, &DeviceObject::finished);
    m_index += m_discovery ? 1 : REQUEST_MAX_ITEMS;

    logDebug(m_debug) << this << "request:" << request.toUtf8().constData();
    process->start("sh", {"-c", request});
}

void DeviceObject::parseProperties(void)
{
    QList <QString> list = {"diskName", "portName", "portLink", "portSpeed", "uptime"}; // NOT REVIEWED
    QMap <QString, QVariant> properties;

    for (auto it = m_map.begin(); it != m_map.end(); it++)
    {
        QString value = m_values.value(it.key());

        if (!m_values.contains(it.key()))
            continue;

        switch (list.indexOf(it.value().split('_').value(0)))
        {
            case 0: properties.insert(it.value(), value); break;                                                 // diskName
            case 1: properties.insert(it.value(), value.isEmpty() ? m_portNames.value(it.key()) : value); break; // portName
            case 2: properties.insert(it.value(), value.toInt() == 1); break;                                    // portLink
            case 3: properties.insert(it.value(), value.toInt()); break;                                         // portSpeed
            case 4: properties.insert(it.value(), value.toLongLong() / 100); break;                              // uptime

            default:
            {
                QVariant data = Parser::stringValue(value);
                double divider = m_options.value(it.value()).toMap().value("divider", 1).toDouble();
                properties.insert(it.value(), data.type() == QVariant::Double && divider ? data.toDouble() / divider : data);
                break;
            }
        }
    }

    for (int i = 0; i < m_diskIndex.count(); i++)
    {
        qint64 size = m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_SIZE, m_diskIndex.at(i))).toLongLong();

        if (!size)
            continue;

        properties.insert(m_diskIndex.count() > 1 ? QString("diskUsage_%1").arg(i + 1) : "diskUsage", round(m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_USED, m_diskIndex.at(i))).toLongLong() * 1000.0 / size) / 10);
    }

    if (!m_cpuIndex.isEmpty())
    {
        double value = 0;

        for (int i = 0; i < m_cpuIndex.count(); i++)
            value += m_values.value(QString("%1.%2").arg(OID_HR_PROCESSOR_LOAD, m_cpuIndex.at(i))).toDouble();

        properties.insert("cpuLoad", round(value * 10 / m_cpuIndex.count()) / 10);
    }

    if (!m_memoryIndex.isEmpty())
    {
        qint64 size = m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_SIZE, m_memoryIndex.value(0))).toLongLong();

        if (size)
        {
            qint64 used = m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_USED, m_memoryIndex.value(0))).toLongLong();

            for (int i = 1; i < m_memoryIndex.count(); i++)
                used -= m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_USED, m_memoryIndex.at(i))).toLongLong();

            properties.insert("memoryUsage", round(used * 1000.0 / size) / 10);
        }
    }

    if (m_properties == properties)
        return;

    m_properties = properties;
    emit propertiesUpdated();
}

void DeviceObject::discovery(void)
{
    QList <QString> items = {OID_IF_ALIAS, OID_IF_OPER_STATUS, OID_IF_HIGH_SPEED}, names = {"portName", "portLink", "portSpeed"};
    QMap <QString, QString> types;
    QList <int> disks, ports;
    QJsonArray exposes;

    m_diskIndex.clear();
    m_cpuIndex.clear();
    m_memoryIndex.clear();
    m_list.clear();

    m_portNames.clear();
    m_map.clear();

    for (auto it = m_values.begin(); it != m_values.end(); it++)
    {
        QString index = it.key().split('.').last(), description = m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_DESCR, index)).toLower();

        if (!it.key().startsWith(QString("%1.").arg(OID_HR_STORAGE_TYPE)))
        {
            if (it.key().startsWith(QString("%1.").arg(OID_HR_FS_STORAGE_INDEX)))
                types.insert(it.value(), m_values.value(QString("%1.%2").arg(OID_HR_FS_TYPE, index)));
            else if (it.key().startsWith(QString("%1.").arg(OID_IF_TYPE)) && it.value().toInt() == 6) // ethernetCsmacd
                ports.append(index.toInt());
            else if (it.key().startsWith(QString("%1.").arg(OID_HR_PROCESSOR_LOAD)))
                m_cpuIndex.append(index);
        }
        else
        {
            if (it.value() == QString(".%1").arg(OID_HR_STORAGE_FIXED_DISK))
                disks.append(index.toInt());
            else if (it.value() == QString(".%1").arg(OID_HR_STORAGE_RAM))
                m_memoryIndex.prepend(index);
            else if (description == "memory buffers" || description == "cached memory")
                m_memoryIndex.append(index);
        }
    }

    for (int i = 0; i < disks.count(); i++)
    {
        QString index = QString::number(disks.at(i)), description = m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_DESCR, index)), type = QString(".%1").arg(OID_HR_FS_OTHER);

        if (m_disks == "all" ? description != "/" && types.value(index, type) == type : !m_disks.split(QRegExp("\\s*,\\s*")).contains(description))
            continue;

        m_diskIndex.append(index);
    }

    std::sort(m_diskIndex.begin(), m_diskIndex.end(), [this] (const QString &a, const QString &b) { return m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_DESCR, a)) < m_values.value(QString("%1.%2").arg(OID_HR_STORAGE_DESCR, b)); });

    for (int i = 0; i < m_diskIndex.count(); i++)
    {
        QString item = QString("%1.%2").arg(OID_HR_STORAGE_DESCR, m_diskIndex.at(i)), suffix;

        if (m_diskIndex.count() > 1)
            suffix = QString("_%1").arg(i + 1);

        m_list.append({item, QString("%1.%2").arg(OID_HR_STORAGE_USED, m_diskIndex.at(i)), QString("%1.%2").arg(OID_HR_STORAGE_SIZE, m_diskIndex.at(i))});
        m_map.insert(item, QString("diskName%1").arg(suffix));

        exposes.append(QString("diskName%1").arg(suffix));
        exposes.append(QString("diskUsage%1").arg(suffix));
    }

    if (m_ports != "all")
    {
        QList <QString> list = m_ports.split(',', Qt::SkipEmptyParts);

        for (int i = 0; i < list.count(); i++)
        {
            QList <QString> range = list.at(i).trimmed().split('-');

            for (int j = range.value(0).toInt(); j <= range.value(1, range.value(0)).toInt(); j++)
                ports.append(j);
        }
    }

    std::sort(ports.begin(), ports.end());

    for (int i = 0; i < ports.count(); i++)
    {
        QString index = QString::number(ports.at(i));

        m_portNames.insert(QString("%1.%2").arg(OID_IF_ALIAS, index), m_values.value(QString("%1.%2").arg(OID_IF_NAME, index)));

        for (int j = 0; j < items.count(); j++)
        {
            QString item = QString("%1.%2").arg(items.at(j), index), name = ports.count() > 1 ? QString("%1_%2").arg(names.at(j), index) : names.at(j);
            m_list.append(item);
            m_map.insert(item, name);
            exposes.append(name);
        }
    }

    for (auto it = m_items.begin(); it != m_items.end(); it++)
    {
        m_list.append(it.value().toString());
        m_map.insert(it.value().toString(), it.key());
        exposes.append(it.key());
    }

    if (!m_cpuIndex.isEmpty())
    {
        for (int i = 0; i < m_cpuIndex.count(); i++)
            m_list.append(QString("%1.%2").arg(OID_HR_PROCESSOR_LOAD, m_cpuIndex.at(i)));

        exposes.append("cpuLoad");
    }

    if (!m_memoryIndex.isEmpty())
    {
        m_list.append(QString("%1.%2").arg(OID_HR_STORAGE_SIZE, m_memoryIndex.value(0)));

        for (int i = 0; i < m_memoryIndex.count(); i++)
            m_list.append(QString("%1.%2").arg(OID_HR_STORAGE_USED, m_memoryIndex.at(i)));

        exposes.append("memoryUsage");
    }

    m_list.append(OID_SYS_UPTIME);
    m_map.insert(OID_SYS_UPTIME, "uptime");
    exposes.append("uptime");

    m_pollTime = 0;
    m_discovery = false;

    if (m_ready && m_exposes == exposes)
        return;

    m_ready = true;
    m_published = false;
    m_exposes = exposes;

    emit deviceUpdated();
}

void DeviceObject::finished(int exitCode, QProcess::ExitStatus)
{
    QProcess *process = reinterpret_cast <QProcess*> (sender());
    QByteArray response = process->readAllStandardOutput();
    QRegExp expression("^\\.(\\S+) (?:\"(.*)\"|([^\\s\"]\\S*))$");
    QList <QString> list;

    logDebug(m_debug) << this << "reply:" << (response.isEmpty() ? "(empty)" : response.trimmed().constData());
    process->deleteLater();

    if (exitCode)
    {
        if (exitCode != SNMP_ERROR_PACKET)
        {
            updateAvailability(Availability::Offline);
            m_discovery = true;
            m_polling = false;
            emit pollFinished();
            return;
        }

        logWarning << this << "request error:" << process->readAllStandardError().trimmed().replace('\n', 0x20).constData();
    }

    list = QString(response).split('\n', Qt::SkipEmptyParts);

    for (int i = 0; i < list.count(); i++)
    {
        if (!expression.exactMatch(list.at(i)))
            continue;

        m_values.insert(expression.cap(1), expression.cap(3).isEmpty() ? expression.cap(2) : expression.cap(3));
    }

    if (m_index < static_cast <quint32> (m_list.count()))
    {
        sendRequest();
        return;
    }

    updateAvailability(Availability::Online);
    m_polling = false;

    if (!m_discovery)
        parseProperties();
    else
        discovery();

    emit pollFinished();
}
