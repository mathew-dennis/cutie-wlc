#include <blur.h>

#include <QDebug>
#include <QOpenGLContext>

// How many times the image is halved. More levels blur wider.
static const int LEVELS = 3;
// How far each pass reaches, in texels. More blurs wider, too much shows as
// dots. 1 is the plain dual Kawase filter.
static const float OFFSET = 2.0f;

// One triangle that covers the whole viewport; uv goes from 0 to 1 over it.
// clang-format off
static const char *VERTEX_SHADER = R"(#version 300 es
out vec2 uv;
void main()
{
	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	uv = p;
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
// clang-format on

// The source texture can be bigger than the image in it (see resize()), so
// `area` is the part of it that holds the image, as a fraction of the texture.
// clang-format off
static const char *FRAGMENT_COMMON = R"(#version 300 es
precision highp float;
uniform sampler2D tex;
uniform vec2 texel; // size of one source texel
uniform vec2 area;
uniform float offset;
in vec2 uv;
out vec4 color;

// Never reads outside the image, so old pixels around it do not leak in
vec3 tap(vec2 p)
{
	return texture(tex, clamp(p, 0.5 * texel, area - 0.5 * texel)).rgb;
}
)";
// clang-format on

// Four corners of the destination pixel and its middle
// clang-format off
static const char *FRAGMENT_DOWN = R"(
void main()
{
	vec2 p = uv * area;
	vec2 d = texel * offset;
	vec3 sum = tap(p) * 4.0;
	sum += tap(p + vec2(-d.x, -d.y));
	sum += tap(p + vec2(d.x, -d.y));
	sum += tap(p + vec2(-d.x, d.y));
	sum += tap(p + vec2(d.x, d.y));
	color = vec4(sum / 8.0, 1.0);
}
)";
// clang-format on

// Eight points around the destination pixel, the diagonal ones count double
// clang-format off
static const char *FRAGMENT_UP = R"(
void main()
{
	vec2 p = uv * area;
	vec2 h = 0.5 * texel * offset;
	vec3 sum = tap(p + vec2(-2.0 * h.x, 0.0));
	sum += tap(p + vec2(-h.x, h.y)) * 2.0;
	sum += tap(p + vec2(0.0, 2.0 * h.y));
	sum += tap(p + vec2(h.x, h.y)) * 2.0;
	sum += tap(p + vec2(2.0 * h.x, 0.0));
	sum += tap(p + vec2(h.x, -h.y)) * 2.0;
	sum += tap(p + vec2(0.0, -2.0 * h.y));
	sum += tap(p + vec2(-h.x, -h.y)) * 2.0;
	color = vec4(sum / 12.0, 1.0);
}
)";
// clang-format on

static bool build(QOpenGLShaderProgram &program, const char *fragmentMain)
{
	return program.addShaderFromSourceCode(QOpenGLShader::Vertex,
					       VERTEX_SHADER) &&
	       program.addShaderFromSourceCode(QOpenGLShader::Fragment,
					       QByteArray(FRAGMENT_COMMON) +
						       fragmentMain) &&
	       program.link();
}

BackgroundBlur::BackgroundBlur()
{
	QOpenGLContext *context = QOpenGLContext::currentContext();
	if (!context || !context->isOpenGLES() ||
	    context->format().majorVersion() < 3) {
		qWarning()
			<< "wlc - background blur is off, it needs OpenGL ES 3 but the context is"
			<< (context ? context->format() : QSurfaceFormat());
		return;
	}
	m_gl.initializeOpenGLFunctions();

	if (!build(m_down, FRAGMENT_DOWN) || !build(m_up, FRAGMENT_UP)) {
		qWarning() << "wlc - background blur is off, shaders failed:"
			   << m_down.log() << m_up.log();
		return;
	}
	m_valid = true;
}

BackgroundBlur::~BackgroundBlur()
{
	qDeleteAll(m_levels);
}

void BackgroundBlur::resize(const QSize &framebufferSize)
{
	if (framebufferSize == m_framebufferSize)
		return;
	m_framebufferSize = framebufferSize;

	qDeleteAll(m_levels);
	m_levels.clear();
	for (int i = 0; i < LEVELS; ++i) {
		const int shift = i + 1;
		m_levels << new QOpenGLFramebufferObject(
			QSize(qMax(1, framebufferSize.width() >> shift),
			      qMax(1, framebufferSize.height() >> shift)));
	}
}

// Draws level `from` (its part `fromArea`) into level `to` (`toArea`).
void BackgroundBlur::pass(QOpenGLShaderProgram &program, int from, int to,
			  const QSize &fromArea, const QSize &toArea)
{
	const QSize fromSize = m_levels[from]->size();

	m_levels[to]->bind();
	m_gl.glViewport(0, 0, toArea.width(), toArea.height());

	program.bind();
	program.setUniformValue("tex", 0);
	program.setUniformValue("texel", 1.0f / fromSize.width(),
				1.0f / fromSize.height());
	program.setUniformValue("area",
				float(fromArea.width()) / fromSize.width(),
				float(fromArea.height()) / fromSize.height());
	program.setUniformValue("offset", OFFSET);

	m_gl.glBindTexture(GL_TEXTURE_2D, m_levels[from]->texture());
	m_gl.glDrawArrays(GL_TRIANGLES, 0, 3);
}

BackgroundBlur::Result BackgroundBlur::blur(const QRect &region,
					    const QSize &framebufferSize)
{
	resize(framebufferSize);

	// Size of the image on every level, rounded up
	QSize areas[LEVELS];
	for (int i = 0; i < LEVELS; ++i) {
		const int shift = i + 1;
		areas[i] = QSize(
			qMax(1, (region.width() + (1 << shift) - 1) >> shift),
			qMax(1, (region.height() + (1 << shift) - 1) >> shift));
	}

	GLint viewport[4];
	m_gl.glGetIntegerv(GL_VIEWPORT, viewport);
	m_gl.glDisable(GL_BLEND);

	// Level 0 is a straight copy at half size; the filter of the copy
	// averages 2x2 pixels. GL counts rows from the bottom.
	const int bottom = framebufferSize.height() - region.bottom() - 1;
	m_gl.glBindFramebuffer(
		GL_READ_FRAMEBUFFER,
		QOpenGLContext::currentContext()->defaultFramebufferObject());
	m_gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_levels[0]->handle());
	m_gl.glBlitFramebuffer(region.left(), bottom,
			       region.left() + region.width(),
			       bottom + region.height(), 0, 0, areas[0].width(),
			       areas[0].height(), GL_COLOR_BUFFER_BIT,
			       GL_LINEAR);

	for (int i = 1; i < LEVELS; ++i)
		pass(m_down, i - 1, i, areas[i - 1], areas[i]);
	for (int i = LEVELS - 1; i > 0; --i)
		pass(m_up, i, i - 1, areas[i], areas[i - 1]);

	QOpenGLFramebufferObject::bindDefault();
	m_gl.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	m_gl.glEnable(GL_BLEND);

	Result result;
	result.texture = m_levels[0]->texture();
	result.textureSize = m_levels[0]->size();
	result.area = areas[0];
	return result;
}
