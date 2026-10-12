#include "Communication.hpp"

#include "MathUtil.hpp"
#include "PowerManager.hpp"
#include "InputPowerPolicy.hpp"
#include "can.h"

namespace Communication
{
struct RxData
{
    uint8_t enableDCDC : 1;
    uint8_t systemRestart : 1;
    uint8_t resv0 : 6;
    uint16_t feedbackRefereePowerLimit;
    uint16_t feedbackRefereeEnergyBuffer;
    uint8_t resv1[3];
} __attribute__((packed));

static RxData rxData;

struct TxData
{
    uint8_t errorCode;
    float chassisPower;
    uint16_t chassisPowerLimit;
    uint8_t capEnergy;
} __attribute__((packed));

static TxData txData;

void init()
{
    static_assert(sizeof(RxData) == 8, "RxData size error");
    static_assert(sizeof(TxData) == 8, "TxData size error");

    CAN_FilterTypeDef filter = {
        0x061 << 5,             // filterID HI
        0,                      // filterID LO
        0,                      // filterMask HI
        0,                      // filterMask LO
        CAN_RX_FIFO0,           // FIFO assignment
        CAN_FILTER_FIFO0,       // filterBank number
        CAN_FILTERMODE_IDLIST,  // filter mode
        CAN_FILTERSCALE_16BIT,  // filter size
        CAN_FILTER_ENABLE,      // ENABLE or DISABLE
        0                       // Slave start bank, STM32F334 only has master
    };
    HAL_CAN_ConfigFilter(&hcan, &filter);
    __HAL_CAN_ENABLE_IT(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
    HAL_CAN_Start(&hcan);
}

static CAN_TxHeaderTypeDef txHeader = {
    0x051,         // StdId
    0,             // ExtId
    CAN_ID_STD,    // IDE
    CAN_RTR_DATA,  // RTR
    8,             // DLC
    DISABLE,       // TransmitGlobalTime
};

void feedbackPowerData()
{
    static uint32_t lastFeedbackTickMs = 0;
    const uint32_t now = HAL_GetTick();
    if (!InputPowerPolicy::feedbackDue(now, lastFeedbackTickMs))
        return;
    lastFeedbackTickMs = now;
    txData.errorCode         = PowerManager::Status::status.errorCode | ((uint8_t)!PowerManager::Status::status.outputEnabled) << 7;
    txData.chassisPower      = PowerManager::Status::status.chassisPower;
    txData.chassisPowerLimit = PowerManager::Status::status.chassisPowerLimit;
    txData.capEnergy         = PowerManager::Status::status.capEnergy > 1.0f ? 255 : PowerManager::Status::status.capEnergy * 255;
    // HAL_CAN_AddTxMessage(&hcan, &txHeader, (uint8_t *)&txData, &txMailbox);
    if ((READ_REG(hcan.Instance->TSR) & CAN_TSR_TME0) != 0U)
        SET_BIT(hcan.Instance->TSR, CAN_TSR_ABRQ0);
    hcan.Instance->sTxMailBox[CAN_TX_MAILBOX0].TIR  = ((txHeader.StdId << CAN_TI0R_STID_Pos) | txHeader.RTR);
    hcan.Instance->sTxMailBox[CAN_TX_MAILBOX0].TDTR = (txHeader.DLC);
    WRITE_REG(hcan.Instance->sTxMailBox[CAN_TX_MAILBOX0].TDHR,
              ((uint32_t)(((uint8_t *)&txData)[7]) << CAN_TDH0R_DATA7_Pos) | ((uint32_t)(((uint8_t *)&txData)[6]) << CAN_TDH0R_DATA6_Pos) |
                  ((uint32_t)(((uint8_t *)&txData)[5]) << CAN_TDH0R_DATA5_Pos) | ((uint32_t)(((uint8_t *)&txData)[4]) << CAN_TDH0R_DATA4_Pos));
    WRITE_REG(hcan.Instance->sTxMailBox[CAN_TX_MAILBOX0].TDLR,
              ((uint32_t)(((uint8_t *)&txData)[3]) << CAN_TDL0R_DATA3_Pos) | ((uint32_t)(((uint8_t *)&txData)[2]) << CAN_TDL0R_DATA2_Pos) |
                  ((uint32_t)(((uint8_t *)&txData)[1]) << CAN_TDL0R_DATA1_Pos) | ((uint32_t)(((uint8_t *)&txData)[0]) << CAN_TDL0R_DATA0_Pos));
    SET_BIT(hcan.Instance->sTxMailBox[CAN_TX_MAILBOX0].TIR, CAN_TI0R_TXRQ);

    uint8_t inputFrame[8];
    const auto &control = PowerManager::ControlData::controlData;
    const bool active = control.inputControlEnabled && control.hasCommand &&
        control.enableOutput && PowerManager::Status::status.outputEnabled &&
        PowerManager::Status::status.errorCode == 0;
    if ((READ_REG(hcan.Instance->TSR) & CAN_TSR_TME1) != 0U &&
        InputPowerPolicy::encodeFeedback(PowerManager::SampleManager::ProcessedData::processedData.pReferee,
            PowerManager::Status::status.inputPowerTarget, active, inputFrame))
    {
        hcan.Instance->sTxMailBox[CAN_TX_MAILBOX1].TIR = 0x052U << CAN_TI0R_STID_Pos;
        hcan.Instance->sTxMailBox[CAN_TX_MAILBOX1].TDTR = 8;
        uint32_t low, high;
        std::memcpy(&low, inputFrame, sizeof(low));
        std::memcpy(&high, inputFrame + 4, sizeof(high));
        hcan.Instance->sTxMailBox[CAN_TX_MAILBOX1].TDLR = low;
        hcan.Instance->sTxMailBox[CAN_TX_MAILBOX1].TDHR = high;
        SET_BIT(hcan.Instance->sTxMailBox[CAN_TX_MAILBOX1].TIR, CAN_TI0R_TXRQ);
    }
}

CAN_RxHeaderTypeDef rxHeader;

extern "C"
{
    void CAN_RX0_IRQHandler(void)
    {
        if ((hcan.Instance->RF0R & CAN_RF0R_FMP0) != 0U)
        {
            while (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, (uint8_t *)&rxData) == HAL_OK)
                if ((rxHeader.StdId == 0x061) && (rxHeader.DLC == 8) && (rxHeader.IDE == CAN_ID_STD))
                {
                    InputPowerPolicy::Command command;
                    if (!InputPowerPolicy::decode(reinterpret_cast<const uint8_t *>(&rxData), 8, command))
                        continue;
                    if (command.restart)
                        PowerManager::systemRestart();

                    auto &control = PowerManager::ControlData::controlData;
                    /* TIM2/HRTIM can latch a fault at higher IRQ priority.
                     * Keep the command transition atomic, restoring the
                     * caller's interrupt mask rather than blindly enabling. */
                    const uint32_t previousPrimask = __get_PRIMASK();
                    __disable_irq();
                    const bool inputModeChanged = control.inputControlEnabled != command.extended;
                    control.refereePowerLimit = command.referee_limit_w;
                    control.energyRemain = command.buffer_energy_j;
                    const bool sourceWasFresh = InputPowerPolicy::commandFresh(
                        control.hasCommand, HAL_GetTick(), control.lastCommandTickMs);
                    if (!command.enabled || !control.lastCommandEnabled || !sourceWasFresh)
                        control.enableOutput = InputPowerPolicy::nextOutputEnable(
                            control.enableOutput, control.lastCommandEnabled, command.enabled,
                            sourceWasFresh, PowerManager::Status::status.errorCode != 0,
                            control.outputReady);
                    control.lastCommandEnabled = command.enabled;
                    control.inputControlEnabled = command.extended;
                    control.inputPowerTarget = command.target_input_w;
                    control.hasCommand = true;
                    control.lastCommandTickMs = HAL_GetTick();
                    __set_PRIMASK(previousPrimask);

                    PowerManager::updateEnergy(inputModeChanged);
                }
        }
    }
}

}  // namespace Communication
