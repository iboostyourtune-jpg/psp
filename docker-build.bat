@echo off
REM One-click build script for Windows using Docker Desktop and PSPSDK image
echo Building PSP EBOOT.PBP inside Docker...
docker run --rm -v %cd%:/work -w /work pspdev/pspsdk bash -lc "make clean && make && mkdir -p /work/out && cp EBOOT.PBP /work/out/EBOOT.PBP"
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b %errorlevel%
)
echo.
echo Build finished successfully!
echo You can find your EBOOT.PBP in the "out" folder.
pause
