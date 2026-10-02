#include <blur.h>

#include <QOpenGLContext>
#include <QDebug>
#include <QVector2D>
#include <QVector4D>

static const char *vertexSrc = R"(#version 300 es
uniform vec4 u_uv; // uv = u_uv.xy + corner * u_uv.zw
out vec2 v_uv;
void main()
{
	vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
	gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
	v_uv = u_uv.xy + corner * u_uv.zw;
}
)";

static const char *downSrc = R"(#version 300 es
precision highp float;
uniform sampler2D u_tex;
uniform vec2 u_halfpixel; // 0.5 / destination size
in vec2 v_uv;
out vec4 o_color;
void main()
{
	vec4 sum = texture(u_tex, v_uv) * 4.0;
	sum += texture(u_tex, v_uv - u_halfpixel);
	sum += texture(u_tex, v_uv + u_halfpixel);
	sum += texture(u_tex, v_uv + vec2(u_halfpixel.x, -u_halfpixel.y));
	sum += texture(u_tex, v_uv - vec2(u_halfpixel.x, -u_halfpixel.y));
	o_color = vec4(sum.rgb / 8.0, 1.0);
}
)";

static const char *upSrc = R"(#version 300 es
precision highp float;
uniform sampler2D u_tex;
uniform vec2 u_halfpixel; // 0.5 / source size
in vec2 v_uv;
out vec4 o_color;
void main()
{
	vec4 sum = texture(u_tex, v_uv + vec2(-u_halfpixel.x * 2.0, 0.0));
	sum += texture(u_tex, v_uv + vec2(-u_halfpixel.x, u_halfpixel.y)) * 2.0;
	sum += texture(u_tex, v_uv + vec2(0.0, u_halfpixel.y * 2.0));
	sum += texture(u_tex, v_uv + vec2(u_halfpixel.x, u_halfpixel.y)) * 2.0;
	sum += texture(u_tex, v_uv + vec2(u_halfpixel.x * 2.0, 0.0));
	sum += texture(u_tex, v_uv + vec2(u_halfpixel.x, -u_halfpixel.y)) * 2.0;
	sum += texture(u_tex, v_uv + vec2(0.0, -u_halfpixel.y * 2.0));
	sum += texture(u_tex, v_uv + vec2(-u_halfpixel.x, -u_halfpixel.y)) * 2.0;
	o_color = vec4(sum.rgb / 12.0, 1.0);
}
)";

static const char *compositeSrc = R"(#version 300 es
precision highp float;
uniform sampler2D u_tex;
in vec2 v_uv;
out vec4 o_color;
void main()
{
	o_color = vec4(texture(u_tex, v_uv).rgb, 1.0);
}
)";

static bool buildProgram(QOpenGLShaderProgram &prog, const char *fragment,
			 const char *name)
{
	if (!prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSrc) ||
	    !prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) ||
	    !prog.link()) {
		qWarning() << "Blur:" << name
			   << "shader failed:" << prog.log();
		return false;
	}
	return true;
}

void BlurRenderer::init()
{
	QOpenGLContext *ctx = QOpenGLContext::currentContext();
	if (!ctx || !ctx->isOpenGLES() || ctx->format().majorVersion() < 3) {
		qWarning() << "Blur: needs an OpenGL ES 3.0 context, blur disabled";
		return;
	}

	initializeOpenGLFunctions();

	if (!buildProgram(m_down, downSrc, "down") ||
	    !buildProgram(m_up, upSrc, "up") ||
	    !buildProgram(m_composite, compositeSrc, "composite"))
		return;

	m_enabled = true;
}

bool BlurRenderer::isEnabled() const
{
	return m_enabled;
}

void BlurRenderer::resizeLevels(const QSize &captureSize)
{
	if (captureSize == m_captureSize)
		return;
	m_captureSize = captureSize;

	QSize size = captureSize;
	for (Level &level : m_levels) {
		size = QSize(qMax(1, (size.width() + 1) / 2),
			     qMax(1, (size.height() + 1) / 2));
		level.size = size;

		if (!level.tex) {
			glGenTextures(1, &level.tex);
			glGenFramebuffers(1, &level.fbo);
		}
		glBindTexture(GL_TEXTURE_2D, level.tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(),
			     size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
			     nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
				GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
				GL_CLAMP_TO_EDGE);
		glBindFramebuffer(GL_FRAMEBUFFER, level.fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				       GL_TEXTURE_2D, level.tex, 0);
	}
}

void BlurRenderer::pass(QOpenGLShaderProgram &prog, const Level &src,
			const Level &dst)
{
	glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
	glViewport(0, 0, dst.size.width(), dst.size.height());
	glBindTexture(GL_TEXTURE_2D, src.tex);
	prog.bind();
	prog.setUniformValue("u_tex", 0);
	prog.setUniformValue("u_uv", QVector4D(0, 0, 1, 1));
	const QSize ref = (&prog == &m_down) ? dst.size : src.size;
	prog.setUniformValue("u_halfpixel", QVector2D(0.5f / ref.width(),
						      0.5f / ref.height()));
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void BlurRenderer::render(const QRegion &glRegion, const QSize &fbSize,
			  GLuint targetFbo)
{
	if (!m_enabled || glRegion.isEmpty())
		return;

	// Capture the region plus some padding so the edges blur against real
	// content instead of clamped texels.
	const QRect bounds = glRegion.boundingRect()
				     .adjusted(-PADDING, -PADDING, PADDING,
					       PADDING)
				     .intersected(QRect(QPoint(), fbSize));
	if (bounds.isEmpty())
		return;

	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	glDisable(GL_BLEND);
	glActiveTexture(GL_TEXTURE0);
	resizeLevels(bounds.size());

	// Capture, downsampled by 2 by the blit itself.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, targetFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_levels[0].fbo);
	glBlitFramebuffer(bounds.x(), bounds.y(), bounds.x() + bounds.width(),
			  bounds.y() + bounds.height(), 0, 0,
			  m_levels[0].size.width(), m_levels[0].size.height(),
			  GL_COLOR_BUFFER_BIT, GL_LINEAR);

	for (int i = 1; i < LEVELS; i++)
		pass(m_down, m_levels[i - 1], m_levels[i]);
	for (int i = LEVELS - 1; i > 0; i--)
		pass(m_up, m_levels[i], m_levels[i - 1]);

	// Put the blurred backdrop back, limited to the requested rectangles.
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	glViewport(0, 0, fbSize.width(), fbSize.height());
	glBindTexture(GL_TEXTURE_2D, m_levels[0].tex);
	m_composite.bind();
	m_composite.setUniformValue("u_tex", 0);
	m_composite.setUniformValue(
		"u_uv", QVector4D(-float(bounds.x()) / bounds.width(),
				  -float(bounds.y()) / bounds.height(),
				  float(fbSize.width()) / bounds.width(),
				  float(fbSize.height()) / bounds.height()));
	glEnable(GL_SCISSOR_TEST);
	for (const QRect &r : glRegion) {
		glScissor(r.x(), r.y(), r.width(), r.height());
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	glDisable(GL_SCISSOR_TEST);

	// Restore what GlWindow::paintGL expects.
	m_composite.release();
	glBindTexture(GL_TEXTURE_2D, 0);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
