#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QSocketNotifier>
#include <QFileSystemWatcher>

// Relative motion straight from the mouse evdev devices. Qt clamps the pointer
// it reports to the primary screen, so this is the only way to know the mouse
// keeps moving past the edge of the phone screen.
class RawPointer : public QObject {
	Q_OBJECT
    public:
	explicit RawPointer(QObject *parent = nullptr);
	~RawPointer();

	int deviceCount() const;
	// Takes ownership of fd, which must be non-blocking.
	void attach(int fd, const QString &path);

    signals:
	void motion(int dx, int dy);

    private:
	void scan();
	void readEvents(int fd);
	void detach(int fd);

	QHash<int, QSocketNotifier *> m_notifiers;
	QHash<int, QString> m_paths;
	QFileSystemWatcher m_watcher;
};
