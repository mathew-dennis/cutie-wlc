#include <rawpointer.h>

#include <QDebug>
#include <QDir>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LONG_BITS (8 * sizeof(unsigned long))
#define BIT_WORDS(n) (((n) + LONG_BITS - 1) / LONG_BITS)

static bool testBit(int bit, const unsigned long *array)
{
	return array[bit / LONG_BITS] & (1UL << (bit % LONG_BITS));
}

// A mouse reports relative X/Y and a left button. Touchscreens and touchpads
// report absolute positions and are left to Qt.
static bool isMouse(int fd)
{
	unsigned long ev[BIT_WORDS(EV_MAX + 1)] = {};
	unsigned long rel[BIT_WORDS(REL_MAX + 1)] = {};
	unsigned long key[BIT_WORDS(KEY_MAX + 1)] = {};
	if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 ||
	    !testBit(EV_REL, ev) || !testBit(EV_KEY, ev))
		return false;
	ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel);
	ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key);
	return testBit(REL_X, rel) && testBit(REL_Y, rel) &&
	       testBit(BTN_LEFT, key);
}

RawPointer::RawPointer(QObject *parent)
	: QObject(parent)
{
	scan();
	m_watcher.addPath(QStringLiteral("/dev/input"));
	connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
		&RawPointer::scan);
}

RawPointer::~RawPointer()
{
	for (int fd : m_notifiers.keys())
		::close(fd);
}

int RawPointer::deviceCount() const
{
	return m_notifiers.size();
}

void RawPointer::scan()
{
	const QDir dir(QStringLiteral("/dev/input"));
	for (const QString &name :
	     dir.entryList({ QStringLiteral("event*") }, QDir::System)) {
		const QString path = dir.filePath(name);
		if (m_paths.values().contains(path))
			continue;
		const int fd = ::open(path.toLocal8Bit().constData(),
				      O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (!isMouse(fd)) {
			::close(fd);
			continue;
		}
		attach(fd, path);
	}
}

void RawPointer::attach(int fd, const QString &path)
{
	char name[128] = {};
	ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
	qInfo() << "Cutie: raw pointer device" << path << name;

	auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
	connect(notifier, &QSocketNotifier::activated, this,
		[this, fd]() { readEvents(fd); });
	m_notifiers.insert(fd, notifier);
	m_paths.insert(fd, path);
}

void RawPointer::detach(int fd)
{
	QSocketNotifier *notifier = m_notifiers.take(fd);
	m_paths.remove(fd);
	if (notifier) {
		notifier->setEnabled(false);
		notifier->deleteLater();
	}
	::close(fd);
}

void RawPointer::readEvents(int fd)
{
	input_event events[64];
	for (;;) {
		const ssize_t bytes = ::read(fd, events, sizeof(events));
		if (bytes < 0 && (errno == EAGAIN || errno == EINTR))
			return;
		if (bytes <= 0) {
			// Unplugged.
			detach(fd);
			return;
		}

		int dx = 0;
		int dy = 0;
		const size_t count = bytes / sizeof(input_event);
		for (size_t i = 0; i < count; ++i) {
			const input_event &e = events[i];
			if (e.type == EV_REL && e.code == REL_X)
				dx += e.value;
			else if (e.type == EV_REL && e.code == REL_Y)
				dy += e.value;
			else if (e.type == EV_SYN && e.code == SYN_REPORT &&
				 (dx || dy)) {
				emit motion(dx, dy);
				dx = dy = 0;
			}
		}
		if (dx || dy)
			emit motion(dx, dy);
	}
}
