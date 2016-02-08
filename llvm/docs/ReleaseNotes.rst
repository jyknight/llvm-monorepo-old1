======================
LLVM 3.8 Release Notes
======================

.. contents::
    :local:

.. warning::
   These are in-progress notes for the upcoming LLVM 3.8 release.  You may
   prefer the `LLVM 3.7 Release Notes <http://llvm.org/releases/3.7.0/docs
   /ReleaseNotes.html>`_.


Introduction
============

This document contains the release notes for the LLVM Compiler Infrastructure,
release 3.8.  Here we describe the status of LLVM, including major improvements
from the previous release, improvements in various subprojects of LLVM, and
some of the current users of the code.  All LLVM releases may be downloaded
from the `LLVM releases web site <http://llvm.org/releases/>`_.

For more information about LLVM, including information about the latest
release, please check out the `main LLVM web site <http://llvm.org/>`_.  If you
have questions or comments, the `LLVM Developer's Mailing List
<http://lists.llvm.org/mailman/listinfo/llvm-dev>`_ is a good place to send
them.

Note that if you are reading this file from a Subversion checkout or the main
LLVM web page, this document applies to the *next* release, not the current
one.  To see the release notes for a specific release, please see the `releases
page <http://llvm.org/releases/>`_.

Non-comprehensive list of changes in this release
=================================================
* With this release, the minimum Windows version required for running LLVM is
  Windows 7. Earlier versions, including Windows Vista and XP are no longer
  supported.

* With this release, the autoconf build system is deprecated. It will be removed
  in the 3.9 release. Please migrate to using CMake. For more information see:
  `Building LLVM with CMake <CMake.html>`_

* The C API function LLVMLinkModules is deprecated. It will be removed in the
  3.9 release. Please migrate to LLVMLinkModules2. Unlike the old function the
  new one

   * Doesn't take an unused parameter.
   * Destroys the source instead of only damaging it.
   * Does not record a message. Use the diagnostic handler instead.

* The C API functions LLVMParseBitcode, LLVMParseBitcodeInContext,
  LLVMGetBitcodeModuleInContext and LLVMGetBitcodeModule have been deprecated.
  They will be removed in 3.9. Please migrate to the versions with a 2 suffix.
  Unlike the old ones the new ones do not record a diagnostic message. Use
  the diagnostic handler instead.

* The deprecated C APIs LLVMGetBitcodeModuleProviderInContext and
  LLVMGetBitcodeModuleProvider have been removed.

* The deprecated C APIs LLVMCreateExecutionEngine, LLVMCreateInterpreter,
  LLVMCreateJITCompiler, LLVMAddModuleProvider and LLVMRemoveModuleProvider
  have been removed.

* With this release, the C API headers have been reorganized to improve build
  time. Type specific declarations have been moved to Type.h, and error
  handling routines have been moved to ErrorHandling.h. Both are included in
  Core.h so nothing should change for projects directly including the headers,
  but transitive dependencies may be affected.

* llvm-ar now suports thin archives.

* llvm doesn't produce .data.rel.ro.local or .data.rel sections anymore.

* aliases to available_externally globals are now rejected by the verifier.

* the IR Linker has been split into IRMover that moves bits from one module to
  another and Linker proper that decides what to link.

* Support for dematerializing has been dropped.

.. NOTE
   For small 1-3 sentence descriptions, just add an entry at the end of
   this list. If your description won't fit comfortably in one bullet
   point (e.g. maybe you would like to give an example of the
   functionality, or simply have a lot to talk about), see the `NOTE` below
   for adding a new subsection.

* ... next change ...

.. NOTE
   If you would like to document a larger change, then you can add a
   subsection about it right here. You can copy the following boilerplate
   and un-indent it (the indentation causes it to be inside this comment).

   Special New Feature
   -------------------

   Makes programs 10x faster by doing Special New Thing.

Changes to the ARM Backend
--------------------------

 During this release ...


Changes to the MIPS Target
--------------------------

During this release the MIPS target has:

* Significantly extended support for the Integrated Assembler. See below for
  more information
* Added support for the ``P5600`` processor.
* Added support for the ``interrupt`` attribute for MIPS32R2 and later. This
  attribute will generate a function which can be used as a interrupt handler
  on bare metal MIPS targets using the static relocation model.
* Added support for the ``ERETNC`` instruction found in MIPS32R5 and later.
* Added support for OpenCL. See http://portablecl.org/.

  * Address spaces 1 to 255 are now reserved for software use and conversions
    between them are no-op casts.

* Removed the ``mips16`` value for the -mcpu option since it is an :abbr:`ASE
  (Application Specific Extension)` and not a processor. If you were using this,
  please specify another CPU and use ``-mips16`` to enable MIPS16.
* Removed ``copy_u.w`` from 32-bit MSA and ``copy_u.d`` from 64-bit MSA since
  they have been removed from the MSA specification due to forward compatibility
  issues.  For example, 32-bit MSA code containing ``copy_u.w`` would behave
  differently on a 64-bit processor supporting MSA. The corresponding intrinsics
  are still available and may expand to ``copy_s.[wd]`` where this is
  appropriate for forward compatibility purposes.
* Relaxed the ``-mnan`` option to allow ``-mnan=2008`` on MIPS32R2/MIPS64R2 for
  compatibility with GCC.
* Made MIPS64R6 the default CPU for 64-bit Android triples.

The MIPS target has also fixed various bugs including the following notable
fixes:

* Fixed reversed operands on ``mthi``/``mtlo`` in the DSP :abbr:`ASE
  (Application Specific Extension)`.
* The code generator no longer uses ``jal`` for calls to absolute immediate
  addresses.
* Disabled fast instruction selection on MIPS32R6 and MIPS64R6 since this is not
  yet supported.
* Corrected addend for ``R_MIPS_HI16`` and ``R_MIPS_PCHI16`` in MCJIT
* The code generator no longer crashes when handling subregisters of an 64-bit
  FPU register with undefined value.
* The code generator no longer attempts to use ``$zero`` for operands that do
  not permit ``$zero``.
* Corrected the opcode used for ``ll``/``sc`` when using MIPS32R6/MIPS64R6 and
  the Integrated Assembler.
* Added support for atomic load and atomic store.
* Corrected debug info when dynamically re-aligning the stack.
 
Integrated Assembler
^^^^^^^^^^^^^^^^^^^^
We have made a large number of improvements to the integrated assembler for
MIPS. In this release, the integrated assembler isn't quite production-ready
since there are a few known issues related to bare-metal support, checking
immediates on instructions, and the N32/N64 ABI's. However, the current support
should be sufficient for many users of the O32 ABI, particularly those targeting
MIPS32 on Linux or bare-metal MIPS32.

If you would like to try the integrated assembler, please use
``-fintegrated-as``.

Changes to the PowerPC Target
-----------------------------

 During this release ...


Changes to the X86 Target
-----------------------------

 During this release ...

* TLS is enabled for Cygwin as emutls.


Changes to the OCaml bindings
-----------------------------

 During this release ...

* The ocaml function link_modules has been replaced with link_modules' which
  uses LLVMLinkModules2.


External Open Source Projects Using LLVM 3.8
============================================

An exciting aspect of LLVM is that it is used as an enabling technology for
a lot of other language and tools projects. This section lists some of the
projects that have already been updated to work with LLVM 3.8.

* A project


Additional Information
======================

A wide variety of additional information is available on the `LLVM web page
<http://llvm.org/>`_, in particular in the `documentation
<http://llvm.org/docs/>`_ section.  The web page also contains versions of the
API documentation which is up-to-date with the Subversion version of the source
code.  You can access versions of these documents specific to this release by
going into the ``llvm/docs/`` directory in the LLVM tree.

If you have any questions or comments about LLVM, please feel free to contact
us via the `mailing lists <http://llvm.org/docs/#maillist>`_.

