@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================================
rem  serverstart.cmd - build, initialize the database, and start the dedicated
rem  DeepspaceOutpost server in one step.
rem
rem  It performs, in order:
rem    1. Configure + build the Server and dbseed targets with the SQL Server
rem       (ODBC) persistence backend on (-DDSO_ENABLE_ODBC=ON).
rem    2. Create the "Deepspace" database if it does not exist.
rem    3. Apply the persistence schema (NeuronServer\schema.sql).
rem    4. Reset all persisted gameplay data in the schema.
rem    5. Seed the galaxy (dbo.systems + baseline markets) with the dbseed tool.
rem    6. Start the server with DSO_DB pointed at the Deepspace database, so it
rem       loads/saves through SQL Server.
rem
rem  Database   : Deepspace
rem  Login      : DSAdmin / DSAdmin  (SQL Server authentication)
rem  WARNING    : Persisted gameplay data is deleted and reseeded on every run.
rem
rem  Requires the MSVC/CMake build environment on PATH (run from a "Developer
rem  Command Prompt", or after vcvarsall.bat), plus sqlcmd and an ODBC driver for
rem  SQL Server. Any of the DSO_* / DSO_PRESET / DSO_SQLSERVER values below can be
rem  overridden in the environment before invoking this script.
rem ============================================================================

set "ROOT=%~dp0"
pushd "%ROOT%"

rem --- Tunables (override via the environment) --------------------------------
if not defined DSO_PRESET      set "DSO_PRESET=x64-release"
if not defined DSO_SQLSERVER   set "DSO_SQLSERVER=localhost"
if not defined DSO_DBNAME      set "DSO_DBNAME=Deepspace"
if not defined DSO_DBUSER      set "DSO_DBUSER=DSAdmin"
if not defined DSO_DBPASS      set "DSO_DBPASS=DSAdmin"
if not defined DSO_ODBC_DRIVER set "DSO_ODBC_DRIVER=ODBC Driver 17 for SQL Server"

set "BUILD_DIR=%ROOT%out\build\%DSO_PRESET%"

rem The ODBC connection string the server (and dbseed) read from DSO_DB.
set "DSO_DB=Driver={%DSO_ODBC_DRIVER%};Server=%DSO_SQLSERVER%;Database=%DSO_DBNAME%;Uid=%DSO_DBUSER%;Pwd=%DSO_DBPASS%;"

rem ============================================================================
rem  1. Build (Server + dbseed) with the ODBC persistence backend enabled.
rem ============================================================================
echo(
echo === [1/6] Configuring and building (preset %DSO_PRESET%, ODBC on) ===
cmake --preset %DSO_PRESET% -DDSO_ENABLE_ODBC=ON
if errorlevel 1 goto :fail

cmake --build --preset %DSO_PRESET% --target Server dbseed
if errorlevel 1 goto :fail

rem Locate the freshly built target executables. Do not recursively search the
rem full build tree: dependency subbuilds can contain temporary paths with the
rem same executable names.
set "SERVER_EXE=%BUILD_DIR%\Server\Server.exe"
set "DBSEED_EXE=%BUILD_DIR%\tools\dbseed\dbseed.exe"

if not exist "%SERVER_EXE%" (
  echo ERROR: Server.exe not found under "%BUILD_DIR%" after the build.
  goto :fail
)
if not exist "%DBSEED_EXE%" (
  echo ERROR: dbseed.exe not found under "%BUILD_DIR%" after the build.
  goto :fail
)

rem ============================================================================
rem  2. Create the database if it is not already there. If this fails with
rem     CREATE DATABASE permission denied, create the empty database once using
rem     an admin login, then rerun this script with the normal DSO_DBUSER.
rem ============================================================================
echo(
echo === [2/6] Ensuring database "%DSO_DBNAME%" exists on %DSO_SQLSERVER% ===
sqlcmd -S "%DSO_SQLSERVER%" -U "%DSO_DBUSER%" -P "%DSO_DBPASS%" -d master -b -Q ^
  "IF DB_ID(N'%DSO_DBNAME%') IS NULL BEGIN CREATE DATABASE [%DSO_DBNAME%]; PRINT 'Created database %DSO_DBNAME%.'; END ELSE PRINT 'Database %DSO_DBNAME% already exists.';"
if errorlevel 1 goto :fail

rem ============================================================================
rem  3. Apply the persistence schema (idempotent - every object is guarded).
rem ============================================================================
echo(
echo === [3/6] Applying schema (NeuronServer\schema.sql) ===
sqlcmd -S "%DSO_SQLSERVER%" -U "%DSO_DBUSER%" -P "%DSO_DBPASS%" -d "%DSO_DBNAME%" -b -i "%ROOT%NeuronServer\schema.sql"
if errorlevel 1 goto :fail

rem ============================================================================
rem  4. Reset persisted data so every launch starts from clean schema tables.
rem ============================================================================
echo(
echo === [4/6] Resetting persisted database data ===
sqlcmd -S "%DSO_SQLSERVER%" -U "%DSO_DBUSER%" -P "%DSO_DBPASS%" -d "%DSO_DBNAME%" -b -Q "SET NOCOUNT ON; DELETE FROM dbo.player_cargo; DELETE FROM dbo.players; DELETE FROM dbo.empires; DELETE FROM dbo.accounts; DELETE FROM dbo.command_log; DELETE FROM dbo.station_markets; DELETE FROM dbo.systems; DELETE FROM dbo.world_meta WHERE meta_key <> N'schema_version'; DBCC CHECKIDENT (N'dbo.accounts', RESEED, 0) WITH NO_INFOMSGS; DBCC CHECKIDENT (N'dbo.empires', RESEED, 0) WITH NO_INFOMSGS; DBCC CHECKIDENT (N'dbo.players', RESEED, 0) WITH NO_INFOMSGS; DBCC CHECKIDENT (N'dbo.command_log', RESEED, 0) WITH NO_INFOMSGS; PRINT 'Persisted gameplay data reset.';"
if errorlevel 1 goto :fail

rem ============================================================================
rem  5. Seed the galaxy (systems + baseline markets). dbseed reads the same DSO_DB
rem     connection string we exported above.
rem ============================================================================
echo(
echo === [5/6] Seeding the galaxy (dbseed) ===
"%DBSEED_EXE%"
if errorlevel 1 goto :fail

rem ============================================================================
rem  6. Start the server. DSO_DB is set, so persistence is live against Deepspace.
rem ============================================================================
echo(
echo === [6/6] Starting the server (DSO_DB -> %DSO_DBNAME%) ===
echo Server: "%SERVER_EXE%"
"%SERVER_EXE%" %*
set "RC=%ERRORLEVEL%"

popd
endlocal & exit /b %RC%

:fail
echo(
echo serverstart.cmd FAILED (errorlevel %ERRORLEVEL%).
popd
endlocal & exit /b 1
