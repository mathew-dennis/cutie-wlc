#ifndef CWL_BLUR_H
#define CWL_BLUR_H

#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QRegion>
#include <QSize>

// Dual-Kawase blur of whatever is already in the target framebuffer.
// Needs an OpenGL ES 3.0 context; otherwise it stays disabled and render()
// does nothing.
class BlurRenderer : protected QOpenGLExtraFunctions {
    public:
	// Call with the GL context current.
	void init();
	bool isEnabled() const;

	// glRegion: rectangles in framebuffer pixels, origin bottom-left.
	// Blurs the framebuffer content under them, in place.
	void render(const QRegion &glRegion, const QSize &fbSize,
		    GLuint targetFbo);

    private:
	static constexpr int LEVELS = 3;
	static constexpr int PADDING = 32;

	struct Level {
		GLuint fbo = 0;
		GLuint tex = 0;
		QSize size;
	};

	void resizeLevels(const QSize &captureSize);
	void pass(QOpenGLShaderProgram &prog, const Level &src,
		  const Level &dst);

	bool m_enabled = false;
	QOpenGLShaderProgram m_down;
	QOpenGLShaderProgram m_up;
	QOpenGLShaderProgram m_composite;
	Level m_levels[LEVELS];
	QSize m_captureSize;
};

#endif
