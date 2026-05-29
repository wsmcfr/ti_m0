param(
    [switch]$SkipTargetBuild,
    [switch]$RequireTargetBuild,
    [switch]$KeepArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# 保存脚本退出码，任何测试失败都会把它置为非 0。
$exitCode = 0

# 工程根目录由脚本所在目录向上一级推导，避免用户必须站在固定目录执行。
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Resolve-Path (Join-Path $scriptRoot "..")

# 每次运行使用独立构建目录，避免两个脚本实例并发时互相删除测试产物。
$runId = "{0}_{1}" -f (Get-Date -Format "yyyyMMddHHmmss"), ([System.Guid]::NewGuid().ToString("N").Substring(0, 8))
$buildRoot = Join-Path $projectRoot "tests\build"
$buildDir = Join-Path $buildRoot $runId

# 所有主机端测试的编译和运行定义。每项都只覆盖无硬件依赖的纯逻辑或桩测试。
$hostTests = @(
    @{
        Name = "scheduler"
        Output = "scheduler_test.exe"
        Args = @(
            "-std=c99", "-Wall", "-Wextra", "-Werror", "-DSCHEDULER_HOST_TEST",
            "-IUser", "-IUser/App", "-IUser/Driver",
            "tests/scheduler_test.c", "User/scheduler.c"
        )
    },
    @{
        Name = "gyro_protocol"
        Output = "gyro_protocol_test.exe"
        Args = @(
            "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-IUser/App",
            "tests/gyro_protocol_test.c", "User/App/gyro_protocol.c"
        )
    },
    @{
        Name = "gray_sensor_logic"
        Output = "gray_sensor_logic_test.exe"
        Args = @(
            "-std=c99", "-Wall", "-Wextra", "-Werror", "-DLINE_TRACK_HOST_TEST",
            "-IUser/App",
            "tests/gray_sensor_logic_test.c", "User/App/line_track_app.c"
        )
    },
    @{
        Name = "motor_protocol"
        Output = "motor_protocol_test.exe"
        Args = @(
            "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-IUser/App",
            "tests/motor_protocol_test.c", "User/App/motor_protocol.c"
        )
    },
    @{
        Name = "motor_app"
        Output = "motor_app_test.exe"
        Args = @(
            "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-IUser/App", "-IUser/Driver", "-IUser",
            "tests/motor_app_test.c", "User/App/motor_app.c", "User/App/motor_protocol.c"
        )
    },
    @{
        Name = "oled_app_format"
        Output = "oled_app_format_test.exe"
        Args = @(
            "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-IUser/App", "-IUser/Driver", "-IUser",
            "tests/oled_app_format_test.c", "User/App/oled_app.c"
        )
    }
)

function Write-Section {
    param(
        [string]$Title
    )

    Write-Host ""
    Write-Host "== $Title =="
}

function Invoke-CheckedCommand {
    param(
        [string]$Name,
        [string]$FilePath,
        [string[]]$Arguments
    )

    # 通过 Start-Process 捕获退出码，避免 PowerShell 对本地 exe 参数做额外解释。
    $process = Start-Process -FilePath $FilePath `
        -ArgumentList $Arguments `
        -WorkingDirectory $projectRoot `
        -NoNewWindow `
        -PassThru `
        -Wait

    if ($process.ExitCode -ne 0) {
        throw "$Name failed with exit code $($process.ExitCode)"
    }
}

function Test-Tool {
    param(
        [string]$ToolName
    )

    # 返回工具查找结果，调用者根据 null 判断可用性。
    return Get-Command $ToolName -ErrorAction SilentlyContinue
}

Push-Location $projectRoot
try {
    Write-Section "Environment"
    Write-Host "Project: $projectRoot"

    $gcc = Test-Tool "gcc"
    if ($null -eq $gcc) {
        Write-Host "[FAIL] gcc not found. Install MinGW or add gcc to PATH."
        exit 1
    }
    Write-Host "[OK] gcc: $($gcc.Source)"

    # 测试产物统一放到 tests/build/<run-id>，避免污染 tests 根目录。
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

    Write-Section "Host Tests"
    foreach ($test in $hostTests) {
        $outputPath = Join-Path $buildDir $test.Output
        $compileArgs = @($test.Args) + @("-o", $outputPath)

        try {
            Write-Host "[BUILD] $($test.Name)"
            Invoke-CheckedCommand -Name "$($test.Name) build" -FilePath "gcc" -Arguments $compileArgs

            Write-Host "[RUN]   $($test.Name)"
            Invoke-CheckedCommand -Name "$($test.Name) run" -FilePath $outputPath -Arguments @()

            Write-Host "[PASS]  $($test.Name)"
        }
        catch {
            Write-Host "[FAIL]  $($test.Name): $($_.Exception.Message)"
            $exitCode = 1
        }
    }

    Write-Section "Target Build"
    if ($SkipTargetBuild) {
        Write-Host "[SKIP] target build skipped by -SkipTargetBuild"
    }
    else {
        $make = Test-Tool "make"
        if ($null -eq $make) {
            $message = "make not found; target firmware build was not run"
            if ($RequireTargetBuild) {
                Write-Host "[FAIL] $message"
                $exitCode = 1
            }
            else {
                Write-Host "[SKIP] $message"
            }
        }
        else {
            try {
                Write-Host "[BUILD] make -C gcc clean all"
                Invoke-CheckedCommand -Name "target build" -FilePath "make" -Arguments @("-C", "gcc", "clean", "all")
                Write-Host "[PASS] target build"
            }
            catch {
                Write-Host "[FAIL] target build: $($_.Exception.Message)"
                $exitCode = 1
            }
        }
    }

    Write-Section "Summary"
    if ($exitCode -eq 0) {
        Write-Host "[PASS] host self-test completed"
    }
    else {
        Write-Host "[FAIL] one or more checks failed"
    }
}
finally {
    Pop-Location

    if (-not $KeepArtifacts) {
        # 默认清理本次运行的测试 exe，保持工作区干净；失败时也尝试清理已生成产物。
        if (Test-Path $buildDir) {
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        }

        # 如果 tests/build 已经没有其它运行产物，也一并移除空目录。
        if ((Test-Path $buildRoot) -and
            ((Get-ChildItem -LiteralPath $buildRoot -Force | Measure-Object).Count -eq 0)) {
            Remove-Item -LiteralPath $buildRoot -Force
        }
    }
}

exit $exitCode
