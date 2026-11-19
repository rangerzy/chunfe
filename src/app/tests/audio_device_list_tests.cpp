#include "audio_device_list.h"

#include <QMediaDevices>
#include <QSignalSpy>
#include <QtTest>

using namespace AudioPipline;

class AudioDeviceListTests : public QObject
{
    Q_OBJECT

private slots:
    void deviceChangesCoalesceAndRefreshAfterInFlightRequest()
    {
        AudioPiplineManager manager;
        QList<RequestId> requests;
        AudioDeviceList devices(manager, [&] {
            const auto id = QStringLiteral("request-%1").arg(requests.size() + 1);
            requests.append(id);
            return id;
        });
        auto* mediaDevices = devices.findChild<QMediaDevices*>();
        QVERIFY(mediaDevices);

        // Initial/manual refreshes and system changes are one request.
        devices.refresh();
        emit mediaDevices->audioInputsChanged();
        emit mediaDevices->audioOutputsChanged();
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 2000);

        // Repeated changes while enumeration is in progress must not enqueue
        // another request until the first one completes.
        devices.refresh();
        emit mediaDevices->audioInputsChanged();
        emit mediaDevices->audioOutputsChanged();
        QTest::qWait(120);
        QCOMPARE(requests.size(), 1);
        emit manager.requestFinished(QStringLiteral("unrelated"), {});
        QTest::qWait(60);
        QCOMPARE(requests.size(), 1);
        emit manager.requestFinished(requests[0], {});
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 2, 2000);
        emit manager.requestFinished(requests[1], {});
        QTest::qWait(120);
        QCOMPARE(requests.size(), 2);
    }

    void destroyedListIgnoresPendingCompletion()
    {
        AudioPiplineManager manager;
        int requests = 0;
        auto* devices = new AudioDeviceList(manager, [&] {
            ++requests;
            return QStringLiteral("request");
        });
        QTRY_COMPARE_WITH_TIMEOUT(requests, 1, 2000);
        devices->refresh();
        delete devices;
        emit manager.requestFinished(QStringLiteral("request"), {});
        QTest::qWait(120);
        QCOMPARE(requests, 1);
    }

    void mapsDeviceSnapshotsAndNotifiesOnDefaultChanges()
    {
        AudioPiplineManager manager;
        AudioDeviceList devices(manager);

        // Construction reflects the manager's current snapshot, regardless of
        // whether the test machine has physical devices.
        int microphones = 0;
        int outputs = 0;
        const auto initial = manager.environment();
        for (const auto& device : initial.devices) {
            bool supported = false;
            for (const auto& capability : initial.capabilities)
                supported |= capability.source == device.source && capability.supported;
            const bool candidate = !initial.stale && device.available && supported;
            microphones += candidate && device.source == AudioSource::Mic;
            outputs += candidate && device.source == AudioSource::Spk;
        }
        QCOMPARE(devices.microphoneDevices().size(), microphones);
        QCOMPARE(devices.systemDevices().size(), outputs);

        EnvironmentSnapshot snapshot;
        emit manager.environmentChanged(snapshot);
        QVERIFY(devices.microphoneDevices().isEmpty());
        QVERIFY(devices.systemDevices().isEmpty());
        QSignalSpy changed(&devices, &AudioDeviceList::devicesChanged);

        snapshot.devices = {
            {QStringLiteral("mic-id"), QStringLiteral("同名设备"), AudioSource::Mic, true, true},
            {QStringLiteral("spk-id"), QStringLiteral("同名设备"), AudioSource::Spk, false, true},
            {QStringLiteral("ignored"), QStringLiteral("其他"), AudioSource::None, false, true}
        };
        SourceCapabilities micCapability;
        micCapability.source = AudioSource::Mic;
        micCapability.supported = true;
        SourceCapabilities spkCapability;
        spkCapability.source = AudioSource::Spk;
        spkCapability.supported = true;
        snapshot.capabilities = {micCapability, spkCapability};
        emit manager.environmentChanged(snapshot);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(devices.microphoneDevices().size(), 1);
        QCOMPARE(devices.systemDevices().size(), 1);
        const auto microphone = devices.microphoneDevices().first().toMap();
        const auto speaker = devices.systemDevices().first().toMap();
        QCOMPARE(microphone.value(QStringLiteral("id")).toString(), QStringLiteral("mic-id"));
        QCOMPARE(microphone.value(QStringLiteral("name")).toString(), QStringLiteral("同名设备"));
        QVERIFY(microphone.value(QStringLiteral("isDefault")).toBool());
        QCOMPARE(speaker.value(QStringLiteral("id")).toString(), QStringLiteral("spk-id"));
        QCOMPARE(speaker.value(QStringLiteral("name")).toString(), QStringLiteral("同名设备"));
        QVERIFY(!speaker.value(QStringLiteral("isDefault")).toBool());

        // Changes to the default flag alone must propagate to QML.
        snapshot.devices[0].isDefault = false;
        snapshot.devices[1].isDefault = true;
        emit manager.environmentChanged(snapshot);
        QCOMPARE(changed.size(), 2);
        QVERIFY(!devices.microphoneDevices().first().toMap().value(QStringLiteral("isDefault")).toBool());
        QVERIFY(devices.systemDevices().first().toMap().value(QStringLiteral("isDefault")).toBool());
        emit manager.environmentChanged(snapshot);
        QCOMPARE(changed.size(), 2);

        snapshot.devices.clear();
        emit manager.environmentChanged(snapshot);
        QCOMPARE(changed.size(), 3);
        QVERIFY(devices.microphoneDevices().isEmpty());
        QVERIFY(devices.systemDevices().isEmpty());
    }

    void excludesUnavailableUnsupportedUnknownAndStaleDevices()
    {
        AudioPiplineManager manager;
        AudioDeviceList devices(manager);
        EnvironmentSnapshot snapshot;
        SourceCapabilities micCapability;
        micCapability.source = AudioSource::Mic;
        micCapability.supported = true;
        SourceCapabilities spkCapability;
        spkCapability.source = AudioSource::Spk;
        spkCapability.supported = true;
        snapshot.capabilities = {micCapability, spkCapability};
        snapshot.devices = {
            {QStringLiteral("mic"), QStringLiteral("麦克风"), AudioSource::Mic, false, true},
            {QStringLiteral("disconnected"), QStringLiteral("已断开"), AudioSource::Mic, false, false},
            {QStringLiteral("spk"), QStringLiteral("扬声器"), AudioSource::Spk, false, true}
        };
        emit manager.environmentChanged(snapshot);
        QCOMPARE(devices.microphoneDevices().size(), 1);
        QCOMPARE(devices.microphoneDevices().first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("mic"));
        QCOMPARE(devices.systemDevices().size(), 1);
        QSignalSpy changed(&devices, &AudioDeviceList::devicesChanged);

        snapshot.capabilities[1].supported = false;
        emit manager.environmentChanged(snapshot);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(devices.microphoneDevices().size(), 1);
        QVERIFY(devices.systemDevices().isEmpty());

        // A missing capability is unknown, not evidence of support.
        snapshot.capabilities.removeLast();
        emit manager.environmentChanged(snapshot);
        QCOMPARE(changed.size(), 1);
        QVERIFY(devices.systemDevices().isEmpty());

        snapshot.stale = true;
        emit manager.environmentChanged(snapshot);
        QCOMPARE(changed.size(), 2);
        QVERIFY(devices.microphoneDevices().isEmpty());
        QVERIFY(devices.systemDevices().isEmpty());

        snapshot.stale = false;
        snapshot.capabilities.append(spkCapability);
        emit manager.environmentChanged(snapshot);
        QCOMPARE(changed.size(), 3);
        QCOMPARE(devices.microphoneDevices().size(), 1);
        QCOMPARE(devices.systemDevices().size(), 1);
    }
};

QTEST_GUILESS_MAIN(AudioDeviceListTests)
#include "audio_device_list_tests.moc"
