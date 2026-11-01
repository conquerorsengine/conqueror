#include "OpenGLBuffer.h"

#include <glad/glad.h>

#include <algorithm>

namespace Conqueror
{
    // VertexBuffer
    OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size)
        : m_Capacity(size), m_Dynamic(true)
    {
        glCreateBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    }

    OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size)
        : m_Capacity(size)
    {
        glCreateBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
    }

    OpenGLVertexBuffer::~OpenGLVertexBuffer()
    {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLVertexBuffer::Bind() const
    {
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    }

    void OpenGLVertexBuffer::Unbind() const
    {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void OpenGLVertexBuffer::MakeDynamic(const void* fullData)
    {
        m_Dynamic = true;
        if (fullData)
        {
            // Tamamı yazılacak: eski içeriğe gerek yok
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ARRAY_BUFFER, m_Capacity, fullData, GL_DYNAMIC_DRAW);
            return;
        }
        // Kısmi güncelleme: içerik GPU'da geçici tampona kopyalanıp yeniden belirlenen (aynı adlı) depoya geri yazılır.
        // Tampon adı değişmez, VAO bağlantıları geçerli kalır; CPU'ya geri okuma yok (GL 3.3 / GLES 3).
        GLuint temp = 0;
        glGenBuffers(1, &temp);
        glBindBuffer(GL_COPY_WRITE_BUFFER, temp);
        glBufferData(GL_COPY_WRITE_BUFFER, m_Capacity, nullptr, GL_STREAM_COPY);
        glBindBuffer(GL_COPY_READ_BUFFER, m_RendererID);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, m_Capacity);
        glBufferData(GL_COPY_READ_BUFFER, m_Capacity, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_COPY_READ_BUFFER, temp);
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_RendererID);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, m_Capacity);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        glDeleteBuffers(1, &temp);
    }

    void OpenGLVertexBuffer::SetSubData(const void* data, uint32_t offset, uint32_t size)
    {
        if (size == 0)
            return;
        if (!m_Dynamic)
        {
            const bool whole = offset == 0 && size >= m_Capacity;
            MakeDynamic(whole ? data : nullptr);
            if (whole)
                return;
        }
        glNamedBufferSubData(m_RendererID, offset, size, data);
    }

    void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
    {
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        if (size > m_Capacity || !m_Dynamic)
        {
            glBufferData(GL_ARRAY_BUFFER, std::max(size, m_Capacity), nullptr, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
            m_Capacity = std::max(size, m_Capacity);
            m_Dynamic = true;
        }
        else
        {
            glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
        }
    }

    // IndexBuffer
    OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
        : m_Count(count)
        , m_Capacity(count * sizeof(uint32_t))
    {
        glCreateBuffers(1, &m_RendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Capacity, indices, GL_DYNAMIC_DRAW);
    }

    OpenGLIndexBuffer::~OpenGLIndexBuffer()
    {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLIndexBuffer::Bind() const
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    }

    void OpenGLIndexBuffer::Unbind() const
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void OpenGLIndexBuffer::SetData(const void* data, uint32_t count)
    {
        const uint32_t byteSize = count * sizeof(uint32_t);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);

        if (byteSize > m_Capacity)
        {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteSize, data, GL_DYNAMIC_DRAW);
            m_Capacity = byteSize;
        }
        else
        {
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, byteSize, data);
        }

        m_Count = count;
    }
}
