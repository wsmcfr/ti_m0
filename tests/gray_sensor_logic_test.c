/*
 * 灰度循迹算法单元测试。
 * 该测试只验证校准、归一化、数字量转换和线位置误差计算，不访问 ADC/GPIO 硬件。
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "line_track_app.h"

static void test_calibration_update_and_normalize(void)
{
    /* 准备校准数据结构，后续模拟两组采样更新最小/最大值。 */
    line_track_calibration_t calibration;
    /* 第一组原始值作为初始最小/最大。 */
    const uint16_t raw_a[LINE_TRACK_SENSOR_COUNT] = {100, 200, 300, 400, 500, 600, 700, 800};
    /* 第二组原始值扩大范围，用于验证 min/max 都会被更新。 */
    const uint16_t raw_b[LINE_TRACK_SENSOR_COUNT] = {50, 250, 350, 450, 550, 650, 750, 900};
    /* 第三组原始值用于归一化测试。 */
    const uint16_t raw_c[LINE_TRACK_SENSOR_COUNT] = {75, 225, 325, 425, 525, 625, 725, 850};
    uint16_t normalized[LINE_TRACK_SENSOR_COUNT];

    /* 初始化校准结构，确保最小值和最大值处于可更新状态。 */
    LineTrack_InitCalibration(&calibration);
    /* 用两组样本更新校准范围。 */
    LineTrack_UpdateCalibration(&calibration, raw_a, LINE_TRACK_SENSOR_COUNT);
    LineTrack_UpdateCalibration(&calibration, raw_b, LINE_TRACK_SENSOR_COUNT);
    /* 按当前校准范围把原始值映射到 0~1000。 */
    LineTrack_Normalize(raw_c, LINE_TRACK_SENSOR_COUNT, &calibration, normalized, LINE_TRACK_SENSOR_COUNT);

    /* 第 0 路范围 50~100，原始 75 应归一化到 500。 */
    assert(normalized[0] == 500U);
    /* 第 1 路范围 200~250，原始 225 应归一化到 500。 */
    assert(normalized[1] == 500U);
    /* 第 7 路范围 800~900，原始 850 应归一化到 500。 */
    assert(normalized[7] == 500U);
}

static void test_binary_conversion(void)
{
    /* 构造 8 路归一化值，其中大于等于 500 的路应判为压线。 */
    const uint16_t normalized[LINE_TRACK_SENSOR_COUNT] = {0, 499, 500, 700, 1000, 100, 900, 300};
    uint8_t binary[LINE_TRACK_SENSOR_COUNT];

    /* 按阈值 500 转换成数字量。 */
    LineTrack_BuildBinary(normalized, LINE_TRACK_SENSOR_COUNT, 500U, binary, LINE_TRACK_SENSOR_COUNT);

    /* 验证阈值边界：499 为 0，500 为 1。 */
    assert(binary[0] == 0U);
    assert(binary[1] == 0U);
    assert(binary[2] == 1U);
    assert(binary[3] == 1U);
    assert(binary[4] == 1U);
    assert(binary[5] == 0U);
    assert(binary[6] == 1U);
    assert(binary[7] == 0U);
}

static void test_position_error(void)
{
    /* 中间两路压线，理论位置在车体中心，误差应为 0。 */
    const uint8_t centered[LINE_TRACK_SENSOR_COUNT] = {0, 0, 0, 1, 1, 0, 0, 0};
    /* 左侧两路压线，误差应为负数。 */
    const uint8_t left[LINE_TRACK_SENSOR_COUNT] = {0, 1, 1, 0, 0, 0, 0, 0};
    /* 右侧两路压线，误差应为正数。 */
    const uint8_t right[LINE_TRACK_SENSOR_COUNT] = {0, 0, 0, 0, 0, 1, 1, 0};
    /* 完全未检测到线时，算法应返回上一次误差，避免控制量突变。 */
    const uint8_t none[LINE_TRACK_SENSOR_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};

    /* 验证居中状态误差为 0。 */
    assert(LineTrack_CalculatePositionError(centered, LINE_TRACK_SENSOR_COUNT, 123) == 0);
    /* 验证左偏和右偏的符号方向。 */
    assert(LineTrack_CalculatePositionError(left, LINE_TRACK_SENSOR_COUNT, 0) < 0);
    assert(LineTrack_CalculatePositionError(right, LINE_TRACK_SENSOR_COUNT, 0) > 0);
    /* 验证无压线时保留上一次误差。 */
    assert(LineTrack_CalculatePositionError(none, LINE_TRACK_SENSOR_COUNT, -321) == -321);
}

int main(void)
{
    /* 运行校准和归一化测试。 */
    test_calibration_update_and_normalize();
    /* 运行数字量阈值转换测试。 */
    test_binary_conversion();
    /* 运行线位置误差测试。 */
    test_position_error();

    /* 所有 assert 均通过后输出成功提示。 */
    puts("gray_sensor_logic_test: all tests passed");
    /* 返回 0 表示测试正常结束。 */
    return 0;
}
