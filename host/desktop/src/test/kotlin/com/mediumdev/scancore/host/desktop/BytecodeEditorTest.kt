package com.mediumdev.scancore.host.desktop

import org.junit.Assert.assertEquals
import org.junit.Test
import org.objectweb.asm.ClassReader
import org.objectweb.asm.ClassVisitor
import org.objectweb.asm.ClassWriter
import org.objectweb.asm.MethodVisitor
import org.objectweb.asm.Opcodes
import java.net.URLClassLoader
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.Executors
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

// Keiyoushi's minified extensions all name their classes alike
// (eu/kanade/tachiyomi/a/a/a/b): one's interface is another's class. Jars
// converted side by side must each keep to their own.
class BytecodeEditorTest {
    private val shared = "eu/kanade/tachiyomi/a/a/a/b"

    private fun jar(vararg classes: Pair<String, ByteArray>): Path {
        val file = Files.createTempFile("editor", ".jar")
        ZipOutputStream(Files.newOutputStream(file)).use { zip ->
            for ((name, bytes) in classes) {
                zip.putNextEntry(ZipEntry("$name.class"))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
        return file
    }

    private fun anInterface(): ByteArray {
        val cw = ClassWriter(0)
        cw.visit(Opcodes.V1_8, Opcodes.ACC_PUBLIC or Opcodes.ACC_INTERFACE or Opcodes.ACC_ABSTRACT, shared, null, "java/lang/Object", null)
        cw.visitMethod(Opcodes.ACC_PUBLIC or Opcodes.ACC_ABSTRACT, "a", "()V", null, null).visitEnd()
        cw.visitEnd()
        return cw.toByteArray()
    }

    private fun aClass(): ByteArray {
        val cw = ClassWriter(ClassWriter.COMPUTE_MAXS)
        cw.visit(Opcodes.V1_8, Opcodes.ACC_PUBLIC, shared, null, "java/lang/Object", null)
        cw.visitMethod(Opcodes.ACC_PUBLIC, "<init>", "()V", null, null).apply {
            visitCode()
            visitVarInsn(Opcodes.ALOAD, 0)
            visitMethodInsn(Opcodes.INVOKESPECIAL, "java/lang/Object", "<init>", "()V", false)
            visitInsn(Opcodes.RETURN)
            visitMaxs(0, 0)
            visitEnd()
        }
        cw.visitMethod(Opcodes.ACC_PUBLIC, "a", "()V", null, null).apply {
            visitCode()
            visitInsn(Opcodes.RETURN)
            visitMaxs(0, 0)
            visitEnd()
        }
        cw.visitEnd()
        return cw.toByteArray()
    }

    // Makes the shared class and calls its method: what broke.
    private fun aCaller(): ByteArray {
        val cw = ClassWriter(ClassWriter.COMPUTE_MAXS)
        cw.visit(Opcodes.V1_8, Opcodes.ACC_PUBLIC, "eu/kanade/tachiyomi/a/a/a/c", null, "java/lang/Object", arrayOf("java/lang/Runnable"))
        cw.visitMethod(Opcodes.ACC_PUBLIC, "<init>", "()V", null, null).apply {
            visitCode()
            visitVarInsn(Opcodes.ALOAD, 0)
            visitMethodInsn(Opcodes.INVOKESPECIAL, "java/lang/Object", "<init>", "()V", false)
            visitInsn(Opcodes.RETURN)
            visitMaxs(0, 0)
            visitEnd()
        }
        cw.visitMethod(Opcodes.ACC_PUBLIC, "run", "()V", null, null).apply {
            visitCode()
            visitTypeInsn(Opcodes.NEW, shared)
            visitInsn(Opcodes.DUP)
            visitMethodInsn(Opcodes.INVOKESPECIAL, shared, "<init>", "()V", false)
            visitMethodInsn(Opcodes.INVOKEVIRTUAL, shared, "a", "()V", false)
            visitInsn(Opcodes.RETURN)
            visitMaxs(0, 0)
            visitEnd()
        }
        cw.visitEnd()
        return cw.toByteArray()
    }

    // The calls in the caller to the shared class that ended up as calls
    // on an interface.
    private fun interfaceCalls(jar: Path): Int {
        var found = 0
        ZipFile(jar.toFile()).use { zip ->
            ClassReader(zip.getInputStream(zip.getEntry("eu/kanade/tachiyomi/a/a/a/c.class")).readBytes()).accept(
                object : ClassVisitor(Opcodes.ASM9) {
                    override fun visitMethod(
                        access: Int,
                        name: String?,
                        descriptor: String?,
                        signature: String?,
                        exceptions: Array<out String>?,
                    ): MethodVisitor =
                        object : MethodVisitor(Opcodes.ASM9) {
                            override fun visitMethodInsn(
                                opcode: Int,
                                owner: String?,
                                name: String?,
                                descriptor: String?,
                                isInterface: Boolean,
                            ) {
                                if (owner == shared && (isInterface || opcode == Opcodes.INVOKEINTERFACE)) found++
                            }
                        }
                },
                0,
            )
        }
        return found
    }

    @Test
    fun jarsConvertedSideBySideKeepTheirOwnInterfaces() {
        val pool = Executors.newFixedThreadPool(8)
        try {
            repeat(40) {
                val withInterface = List(4) { jar(shared to anInterface()) }
                val withClass = List(4) { jar(shared to aClass(), "eu/kanade/tachiyomi/a/a/a/c" to aCaller()) }
                (withInterface + withClass)
                    .shuffled()
                    .map { file -> pool.submit { BytecodeEditor.fixAndroidClasses(file) } }
                    .forEach { it.get() }
                for (file in withClass) {
                    assertEquals(0, interfaceCalls(file))
                    // And it runs. Closed after, so the jar can go (Windows
                    // keeps an open file from being deleted).
                    URLClassLoader(arrayOf(file.toUri().toURL()), null).use { loader ->
                        (loader.loadClass("eu.kanade.tachiyomi.a.a.a.c").getDeclaredConstructor().newInstance() as Runnable).run()
                    }
                }
                (withInterface + withClass).forEach { Files.deleteIfExists(it) }
            }
        } finally {
            pool.shutdownNow()
        }
    }
}
