/**
 * @file    line_track_app.c
 * @brief   八路灰度循迹应用层实现。
 *
 * @details 本文件分为两部分：前半部分是无硬件依赖的校准、归一化和位置误差算法；
 *          后半部分是周期任务外壳，从灰度驱动读取 8 路 ADC 并更新应用层快照。
 */

#include "line_track_app.h"

#include <string.h>

#if !defined(LINE_TRACK_HOST_TEST)
#include "gray_sensor_driver.h"
#include "scheduler.h"
#endif

/* 位置误差权重。左侧为负，右侧为正，中间两路对称后平均值为 0。 */
static const int16_t s_line_track_weights[LINE_TRACK_SENSOR_COUNT] =
{
    -3500, -2500, -1500, -500, 500, 1500, 2500, 3500
};

#if !defined(LINE_TRACK_HOST_TEST)
/* 应用层保存的校准范围，默认会随着任务持续更新到当前环境范围。 */
static line_track_calibration_t s_line_track_calibration;

/* 应用层最新灰度循迹快照，其他模块通过 LineTrack_AppGetSnapshot() 读取副本。 */
static line_track_snapshot_t s_line_track_latest;

/* 当前二值化阈值，使用 0~1000 归一化量纲。 */
static uint16_t s_line_track_threshold = LINE_TRACK_DEFAULT_THRESHOLD;

/* true 表示周期任务会继续更新校准 min/max，适合上电初期或人工重新标定。 */
static bool s_line_track_calibrating = true;
#endif

/**
 * @brief  初始化灰度校准范围。
 *
 * @note   min_value 置为 uint16 最大值，max_value 置为 0，
 *         这样第一组样本会同时成为每一路的初始最小值和最大值。
 *
 * @param  calibration 需要初始化的校准结构。
 * @return 无。
 */
void LineTrack_InitCalibration(line_track_calibration_t *calibration)
{
    /* 调用者没有提供校准结构时无法初始化，直接返回保护。 */
    if (calibration == NULL)
    {
        /* 空指针没有可写空间。 */
        return;
    }

    /* 初始化每一路的 min/max，让后续第一组样本可以正确收敛到实际值。 */
    for (size_t i = 0U; i < LINE_TRACK_SENSOR_COUNT; i++)
    {
        /* 最小值先放到最大，便于第一次更新时被任意 ADC 样本替换。 */
        calibration->min_value[i] = 0xFFFFU;
        /* 最大值先放到 0，便于第一次更新时被任意 ADC 样本替换。 */
        calibration->max_value[i] = 0U;
    }

    /* 当前还没有任何样本参与校准。 */
    calibration->calibrated = false;
}

/**
 * @brief  用一组原始 ADC 样本更新校准最小/最大值。
 *
 * @note   只处理 raw_count 和固定 8 路数量中的较小者，避免调用者长度不匹配时越界。
 *
 * @param  calibration 需要更新的校准结构。
 * @param  raw         原始 ADC 数组。
 * @param  raw_count   raw 数组元素数量。
 * @return 无。
 */
void LineTrack_UpdateCalibration(line_track_calibration_t *calibration,
    const uint16_t *raw, size_t raw_count)
{
    /* 保存本次实际可处理的通道数。 */
    size_t count = raw_count;

    /* 校准结构或原始样本为空时无法更新。 */
    if ((calibration == NULL) || (raw == NULL))
    {
        /* 参数无效时直接返回。 */
        return;
    }

    /* 限制处理数量到模块固定通道数，保护校准数组边界。 */
    if (count > LINE_TRACK_SENSOR_COUNT)
    {
        /* 调用者传入更长数组时，只使用前 8 路。 */
        count = LINE_TRACK_SENSOR_COUNT;
    }

    /* 逐路更新历史最小值和最大值。 */
    for (size_t i = 0U; i < count; i++)
    {
        /* 如果当前样本更小，则刷新该路校准最小值。 */
        if (raw[i] < calibration->min_value[i])
        {
            /* 记录新的低端校准值。 */
            calibration->min_value[i] = raw[i];
        }

        /* 如果当前样本更大，则刷新该路校准最大值。 */
        if (raw[i] > calibration->max_value[i])
        {
            /* 记录新的高端校准值。 */
            calibration->max_value[i] = raw[i];
        }
    }

    /* 只要至少处理过一路样本，就认为校准范围已有基础数据。 */
    if (count > 0U)
    {
        /* 标记校准结构可供归一化函数使用。 */
        calibration->calibrated = true;
    }
}

/**
 * @brief  按校准范围把原始 ADC 值归一化到 0~1000。
 *
 * @note   对校准范围为 0 的通道返回 0，避免除零；超过范围的输入会被钳制。
 *
 * @param  raw            原始 ADC 数组。
 * @param  raw_count      raw 数组元素数量。
 * @param  calibration    校准范围。
 * @param  out_normalized 输出归一化数组。
 * @param  out_count      输出数组容量。
 * @return 无。
 */
void LineTrack_Normalize(const uint16_t *raw, size_t raw_count,
    const line_track_calibration_t *calibration, uint16_t *out_normalized,
    size_t out_count)
{
    /* 保存本次实际可处理的通道数。 */
    size_t count = raw_count;

    /* 任意关键指针为空时无法转换。 */
    if ((raw == NULL) || (calibration == NULL) || (out_normalized == NULL))
    {
        /* 参数无效时直接返回。 */
        return;
    }

    /* 输出容量可能小于输入长度，实际处理数量取较小值。 */
    if (count > out_count)
    {
        /* 防止写出调用者提供的输出数组。 */
        count = out_count;
    }
    if (count > LINE_TRACK_SENSOR_COUNT)
    {
        /* 防止访问固定 8 路校准数组之外的元素。 */
        count = LINE_TRACK_SENSOR_COUNT;
    }

    /* 逐路执行线性归一化。 */
    for (size_t i = 0U; i < count; i++)
    {
        /* 取出当前通道的低端和高端校准值。 */
        uint16_t min_value = calibration->min_value[i];
        uint16_t max_value = calibration->max_value[i];
        /* 保存该路校准跨度。 */
        uint32_t range;
        /* 保存扣除低端后的有界输入值。 */
        uint32_t offset;

        /* 校准未完成或该路没有有效范围时，输出 0 作为保守值。 */
        if ((calibration->calibrated == false) || (max_value <= min_value))
        {
            /* 无效校准范围不能除法换算。 */
            out_normalized[i] = 0U;
            /* 继续处理下一路。 */
            continue;
        }

        /* 低于校准低端的样本钳制为 0。 */
        if (raw[i] <= min_value)
        {
            /* 当前值在低端以下。 */
            out_normalized[i] = 0U;
            /* 继续处理下一路。 */
            continue;
        }

        /* 高于校准高端的样本钳制为最大归一化值。 */
        if (raw[i] >= max_value)
        {
            /* 当前值在高端以上。 */
            out_normalized[i] = LINE_TRACK_NORMALIZED_MAX;
            /* 继续处理下一路。 */
            continue;
        }

        /* 计算校准跨度和当前样本相对低端的偏移。 */
        range = (uint32_t)max_value - (uint32_t)min_value;
        offset = (uint32_t)raw[i] - (uint32_t)min_value;
        /*
         * 使用整数四舍五入归一化，避免在 MCU 上引入浮点除法。
         * 公式为 offset * 1000 / range，加 range/2 实现四舍五入。
         */
        out_normalized[i] = (uint16_t)(((offset * LINE_TRACK_NORMALIZED_MAX) +
            (range / 2U)) / range);
    }
}

/**
 * @brief  将 0~1000 归一化值按阈值转换为 0/1 数组。
 *
 * @note   大于等于阈值记为 1，小于阈值记为 0。阈值超过 1000 时会被钳制到 1000。
 *
 * @param  normalized       归一化数组。
 * @param  normalized_count normalized 数组元素数量。
 * @param  threshold        二值化阈值。
 * @param  out_binary       输出 0/1 数组。
 * @param  out_count        输出数组容量。
 * @return 无。
 */
void LineTrack_BuildBinary(const uint16_t *normalized, size_t normalized_count,
    uint16_t threshold, uint8_t *out_binary, size_t out_count)
{
    /* 保存本次实际可处理的通道数。 */
    size_t count = normalized_count;

    /* 输入或输出为空时无法转换。 */
    if ((normalized == NULL) || (out_binary == NULL))
    {
        /* 参数无效时直接返回。 */
        return;
    }

    /* 阈值不允许超过归一化最大值，避免所有通道永远为 0。 */
    if (threshold > LINE_TRACK_NORMALIZED_MAX)
    {
        /* 将阈值钳制到合法上限。 */
        threshold = LINE_TRACK_NORMALIZED_MAX;
    }

    /* 输出容量可能小于输入长度，实际处理数量取较小值。 */
    if (count > out_count)
    {
        /* 防止写越界。 */
        count = out_count;
    }
    if (count > LINE_TRACK_SENSOR_COUNT)
    {
        /* 固定只处理 8 路灰度传感器。 */
        count = LINE_TRACK_SENSOR_COUNT;
    }

    /* 逐路按阈值生成 0/1 状态。 */
    for (size_t i = 0U; i < count; i++)
    {
        /* 大于等于阈值判为检测到线，输出 1；否则输出 0。 */
        out_binary[i] = (normalized[i] >= threshold) ? 1U : 0U;
    }
}

/**
 * @brief  把 8 路二值化状态打包成 bit mask。
 *
 * @note   第 i 路对应 bit i，方便 OLED 或串口以紧凑格式显示。
 *
 * @param  binary       0/1 数组。
 * @param  binary_count binary 数组元素数量。
 * @return 8 位状态掩码；参数无效时返回 0。
 */
uint8_t LineTrack_BuildBitMask(const uint8_t *binary, size_t binary_count)
{
    /* 保存实际处理通道数。 */
    size_t count = binary_count;
    /* 保存最终打包结果。 */
    uint8_t mask = 0U;

    /* 输入为空时返回空状态。 */
    if (binary == NULL)
    {
        /* 无输入数据可打包。 */
        return 0U;
    }

    /* 固定最多打包 8 路，防止左移超过 uint8_t 范围。 */
    if (count > LINE_TRACK_SENSOR_COUNT)
    {
        /* 截断到 8 路。 */
        count = LINE_TRACK_SENSOR_COUNT;
    }

    /* 逐路将非零状态写入对应 bit。 */
    for (size_t i = 0U; i < count; i++)
    {
        /* 当前路为 1 时设置对应位。 */
        if (binary[i] != 0U)
        {
            /* 第 i 路对应 bit i。 */
            mask |= (uint8_t)(1U << i);
        }
    }

    /* 返回打包结果。 */
    return mask;
}

/**
 * @brief  根据二值化状态计算线位置误差。
 *
 * @note   对所有检测到线的通道权重求平均。没有任何通道压线时返回 last_error，
 *         这样控制器不会因为短暂丢线立刻跳变到 0。
 *
 * @param  binary       0/1 数组。
 * @param  binary_count binary 数组元素数量。
 * @param  last_error   无线时沿用的上一误差值。
 * @return 当前位置误差，负数偏左，正数偏右。
 */
int16_t LineTrack_CalculatePositionError(const uint8_t *binary,
    size_t binary_count, int16_t last_error)
{
    /* 保存实际处理通道数。 */
    size_t count = binary_count;
    /* 保存命中通道的权重累加值。 */
    int32_t weighted_sum = 0;
    /* 保存检测到线的通道数量。 */
    int32_t active_count = 0;

    /* 输入为空时没有新信息，返回上一误差。 */
    if (binary == NULL)
    {
        /* 保护空指针调用路径。 */
        return last_error;
    }

    /* 固定最多处理 8 路灰度传感器。 */
    if (count > LINE_TRACK_SENSOR_COUNT)
    {
        /* 截断到权重表长度。 */
        count = LINE_TRACK_SENSOR_COUNT;
    }

    /* 逐路累加检测到线的权重。 */
    for (size_t i = 0U; i < count; i++)
    {
        /* 只有二值化为 1 的通道参与位置估计。 */
        if (binary[i] != 0U)
        {
            /* 累加该通道对应的横向权重。 */
            weighted_sum += s_line_track_weights[i];
            /* 记录命中通道数量，用于求平均位置。 */
            active_count++;
        }
    }

    /* 完全未检测到线时沿用上一误差，避免控制输出突变。 */
    if (active_count == 0)
    {
        /* 返回调用者给出的上一误差。 */
        return last_error;
    }

    /* 返回命中通道权重平均值。 */
    return (int16_t)(weighted_sum / active_count);
}

#if !defined(LINE_TRACK_HOST_TEST)
/**
 * @brief  初始化灰度循迹应用层。
 *
 * @note   SysConfig 已经完成 ADC/GPIO 基础配置；这里初始化驱动、校准结构和应用快照。
 *
 * @param  无。
 * @return 无。
 */
void LineTrack_AppInit(void)
{
    /* 初始化底层灰度驱动，确保地址线和 ADC/DMA 状态可用。 */
    GraySensor_DriverInit();
    /* 初始化校准范围，让上电后的采样逐步形成当前环境 min/max。 */
    LineTrack_InitCalibration(&s_line_track_calibration);
    /* 清空最新快照，避免上电后读取到未定义状态。 */
    memset(&s_line_track_latest, 0, sizeof(s_line_track_latest));
    /* 使用默认二值化阈值。 */
    s_line_track_threshold = LINE_TRACK_DEFAULT_THRESHOLD;
    /* 默认进入自动校准状态，便于传感器在启动后先建立范围。 */
    s_line_track_calibrating = true;
}

/**
 * @brief  灰度循迹周期任务。
 *
 * @note   任务读取 8 路灰度原始值，更新校准、归一化、二值化和位置误差。
 *         若底层本次采样失败，任务直接返回并保留上一快照。
 *
 * @param  无。
 * @return 无。
 */
void LineTrack_AppTask(void)
{
    /* 保存本次采样得到的 8 路 ADC 原始值。 */
    uint16_t raw[LINE_TRACK_SENSOR_COUNT];

    /* 从底层驱动读取一整组 8 路传感器值。 */
    if (GraySensor_DriverReadAll(raw, (uint8_t)LINE_TRACK_SENSOR_COUNT) == false)
    {
        /* 采样失败时不更新快照，保留上一组有效数据。 */
        return;
    }

    /* 如果处于校准状态，用本次样本扩展 min/max 范围。 */
    if (s_line_track_calibrating == true)
    {
        /* 持续更新校准范围，便于人工移动小车覆盖黑白区域。 */
        LineTrack_UpdateCalibration(&s_line_track_calibration, raw, LINE_TRACK_SENSOR_COUNT);
    }

    /* 保存原始 ADC 值到最新快照。 */
    memcpy(s_line_track_latest.raw, raw, sizeof(s_line_track_latest.raw));
    /* 将原始值转换到 0~1000 的归一化量纲。 */
    LineTrack_Normalize(s_line_track_latest.raw, LINE_TRACK_SENSOR_COUNT,
        &s_line_track_calibration, s_line_track_latest.normalized,
        LINE_TRACK_SENSOR_COUNT);
    /* 根据阈值生成每一路 0/1 状态。 */
    LineTrack_BuildBinary(s_line_track_latest.normalized, LINE_TRACK_SENSOR_COUNT,
        s_line_track_threshold, s_line_track_latest.binary, LINE_TRACK_SENSOR_COUNT);
    /* 将 8 路二值状态打包成 1 字节掩码，便于显示和调试。 */
    s_line_track_latest.bit_mask = LineTrack_BuildBitMask(s_line_track_latest.binary,
        LINE_TRACK_SENSOR_COUNT);
    /* 计算横向误差；如果本次丢线则沿用上一误差。 */
    s_line_track_latest.position_error = LineTrack_CalculatePositionError(
        s_line_track_latest.binary, LINE_TRACK_SENSOR_COUNT,
        s_line_track_latest.position_error);
    /* 成功更新一次完整快照后累加采样计数。 */
    s_line_track_latest.sample_count++;
    /* 记录当前 tick，供其他模块判断数据新鲜度。 */
    s_line_track_latest.last_update_ms = Scheduler_GetTick();
    /* 标记快照已经有效。 */
    s_line_track_latest.valid = true;
}

/**
 * @brief  获取灰度循迹最新快照。
 *
 * @note   当前工程无 RTOS，快照在主循环任务中更新和读取；如后续中断读取需补临界区。
 *
 * @param  out_snapshot 输出快照结构。
 * @return true 表示输出成功；false 表示参数为空或尚无有效快照。
 */
bool LineTrack_AppGetSnapshot(line_track_snapshot_t *out_snapshot)
{
    /* 调用者必须提供输出结构，且应用层至少成功采样过一次。 */
    if ((out_snapshot == NULL) || (s_line_track_latest.valid == false))
    {
        /* 无法返回有效快照。 */
        return false;
    }

    /* 拷贝最新快照给调用者，避免外部直接修改内部状态。 */
    *out_snapshot = s_line_track_latest;
    /* 返回 true 表示输出已填充。 */
    return true;
}

/**
 * @brief  开始灰度自动校准。
 *
 * @note   调用后会清空旧校准范围，并在后续周期任务中持续用采样更新 min/max。
 *
 * @param  无。
 * @return 无。
 */
void LineTrack_AppStartCalibration(void)
{
    /* 重新初始化校准结构，丢弃旧环境下的 min/max。 */
    LineTrack_InitCalibration(&s_line_track_calibration);
    /* 打开自动校准标志，让周期任务继续更新校准范围。 */
    s_line_track_calibrating = true;
}

/**
 * @brief  停止灰度自动校准。
 *
 * @note   停止后周期任务只使用现有校准范围做归一化，不再扩大 min/max。
 *
 * @param  无。
 * @return 无。
 */
void LineTrack_AppStopCalibration(void)
{
    /* 关闭自动校准标志，固定当前 min/max。 */
    s_line_track_calibrating = false;
}

/**
 * @brief  设置灰度二值化阈值。
 *
 * @note   阈值使用 0~1000 归一化量纲，超过上限会钳制到 1000。
 *
 * @param  threshold 新阈值。
 * @return 无。
 */
void LineTrack_AppSetThreshold(uint16_t threshold)
{
    /* 限制阈值范围，避免无效配置造成所有通道状态异常。 */
    if (threshold > LINE_TRACK_NORMALIZED_MAX)
    {
        /* 超过上限时使用最大合法阈值。 */
        threshold = LINE_TRACK_NORMALIZED_MAX;
    }

    /* 保存新的阈值，下一次任务运行时生效。 */
    s_line_track_threshold = threshold;
}

/**
 * @brief  获取当前灰度二值化阈值。
 *
 * @param  无。
 * @return 当前阈值，量纲为 0~1000。
 */
uint16_t LineTrack_AppGetThreshold(void)
{
    /* 返回当前阈值。 */
    return s_line_track_threshold;
}
#endif
