#ifndef DEVICE_H
#define DEVICE_H

#define REQUEST_MAX_ITEMS           10
#define SNMP_ERROR_PACKET           2

#define OID_SYS_UPTIME              "1.3.6.1.2.1.1.3.0"

#define OID_IF_TYPE                 "1.3.6.1.2.1.2.2.1.3"
#define OID_IF_OPER_STATUS          "1.3.6.1.2.1.2.2.1.8"
#define OID_IF_NAME                 "1.3.6.1.2.1.31.1.1.1.1"
#define OID_IF_HIGH_SPEED           "1.3.6.1.2.1.31.1.1.1.15"
#define OID_IF_ALIAS                "1.3.6.1.2.1.31.1.1.1.18"

#define OID_HR_STORAGE_RAM          "1.3.6.1.2.1.25.2.1.2"
#define OID_HR_STORAGE_FIXED_DISK   "1.3.6.1.2.1.25.2.1.4"
#define OID_HR_STORAGE_ENTRY        "1.3.6.1.2.1.25.2.3.1"
#define OID_HR_STORAGE_TYPE         "1.3.6.1.2.1.25.2.3.1.2"
#define OID_HR_STORAGE_DESCR        "1.3.6.1.2.1.25.2.3.1.3"
#define OID_HR_STORAGE_SIZE         "1.3.6.1.2.1.25.2.3.1.5"
#define OID_HR_STORAGE_USED         "1.3.6.1.2.1.25.2.3.1.6"

#define OID_HR_PROCESSOR_LOAD       "1.3.6.1.2.1.25.3.3.1.2"

#define OID_HR_FS_ENTRY             "1.3.6.1.2.1.25.3.8.1"
#define OID_HR_FS_TYPE              "1.3.6.1.2.1.25.3.8.1.4"
#define OID_HR_FS_STORAGE_INDEX     "1.3.6.1.2.1.25.3.8.1.7"
#define OID_HR_FS_OTHER             "1.3.6.1.2.1.25.3.9.1"

#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>

enum class Availability
{
    Unknown,
    Online,
    Offline
};

class DeviceObject;
typedef QSharedPointer <DeviceObject> Device;

class DeviceObject : public QObject
{
    Q_OBJECT

public:

    DeviceObject(const QString &address, const QString &community, const QString &disks, const QString &ports, const QMap <QString, QVariant> &items, const QMap <QString, QVariant> &options, quint32 pollInterval, const QString &id, bool debug) :
        QObject(nullptr), m_id(id), m_debug(debug), m_address(address), m_community(community), m_disks(disks), m_ports(ports), m_pollInterval(pollInterval), m_pollTime(0), m_discovery(true), m_polling(false), m_ready(false), m_published(false), m_availability(Availability::Unknown), m_items(items), m_options(options) {}

    inline QString id(void) { return m_id; }
    inline QString name(void) { return m_data.value("name").toString(m_id); }

    inline quint32 pollInterval(void) { return m_pollInterval; }
    inline qint64 pollTime(void) { return m_pollTime; }

    inline bool polling(void) { return m_polling; }
    inline bool ready(void) { return m_ready; }

    inline bool published(void) { return m_published; }
    inline void setPublished(bool value) { m_published = value; }

    inline Availability availability(void) { return m_availability; }
    inline QJsonArray exposes(void) { return m_exposes; }

    inline QJsonObject data(void) { return m_data; }
    inline void setData(const QJsonObject &value) { m_data = value; }

    inline QMap <QString, QVariant> properties(void) { return m_properties; }

    QJsonObject options(void);
    void poll(void);

private:

    QString m_id;
    bool m_debug;

    QString m_address, m_community, m_disks, m_ports;
    quint32 m_pollInterval, m_index;
    qint64 m_pollTime;
    bool m_discovery, m_polling, m_ready, m_published;

    Availability m_availability;

    QJsonArray m_exposes;
    QJsonObject m_data;

    QList <QString> m_diskIndex, m_cpuIndex, m_memoryIndex, m_list;
    QMap <QString, QString> m_portNames, m_map, m_values;
    QMap <QString, QVariant> m_items, m_options, m_properties;

    void updateAvailability(Availability availability);
    void sendRequest(void);

    void parseProperties(void);
    void discovery(void);

private slots:

    void finished(int exitCode, QProcess::ExitStatus exitStatus);

signals:

    void deviceUpdated(void);
    void availabilityUpdated(void);
    void propertiesUpdated(void);
    void pollFinished(void);

};

inline QDebug operator << (QDebug debug, DeviceObject *device) { return debug << "device" << device->id(); }

#endif
