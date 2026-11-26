#pragma once

#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QRect>
#include <QSize>

/*
	Blurs a part of the framebuffer that is being drawn (dual Kawase blur).

	The part is copied at half size and shrunk a few more times, then grown
	back with a filter that smears every pixel over its neighbours. Working
	on small textures keeps this cheap enough for a phone GPU.

	Every GlWindow owns one, its textures and shaders belong to the GL
	context of that window. Needs OpenGL ES 3.
*/
class BackgroundBlur {
    public:
	struct Result {
		GLuint texture = 0;
		QSize textureSize;
		// Part of the texture that holds the blurred pixels. It starts
		// in the corner of the texture, the origin is bottom left.
		QSize area;
	};

	// The GL context of the window must be current
	BackgroundBlur();
	~BackgroundBlur();

	bool isValid() const
	{
		return m_valid;
	}

	// Blurs `region` (pixels, origin top left) of the framebuffer being
	// drawn, which is `framebufferSize` big. That framebuffer is bound
	// again afterwards, and blending is on, but the caller must bind its
	// own shader again.
	Result blur(const QRect &region, const QSize &framebufferSize);

    private:
	void resize(const QSize &framebufferSize);
	void pass(QOpenGLShaderProgram &program, int from, int to,
		  const QSize &fromArea, const QSize &toArea);

	QOpenGLExtraFunctions m_gl;
	QOpenGLShaderProgram m_down;
	QOpenGLShaderProgram m_up;
	// Level i is 1/2^(i+1) of the framebuffer. They are made once for the
	// framebuffer size and used for any region of it.
	QList<QOpenGLFramebufferObject *> m_levels;
	QSize m_framebufferSize;
	bool m_valid = false;
};
