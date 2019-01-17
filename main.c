/**
 * Copyright (c) 2014 - 2018, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/** @file
 *
 * @defgroup ble_sdk_app_template_main main.c
 * @{
 * @ingroup ble_sdk_app_template
 * @brief Template project main file.
 *
 * This file contains a template for creating a new application. It has the code necessary to wakeup
 * from button, advertise, get a connection restart advertising on disconnect and if no new
 * connection created go back to system-off mode.
 * It can easily be used as a starting point for creating a new application, the comments identified
 * with 'YOUR_JOB' indicates where and how you can customize.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "nordic_common.h"
#include "nrf.h"
#include "app_error.h"
#include "ble.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_ble.h"
#include "app_timer.h"
#include "fds.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"
#include "bsp_btn_ble.h"
#include "sensorsim.h"
#include "ble_conn_state.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_pwr_mgmt.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include <time.h>
#include "SEGGER_RTT.h"
#include "mible_log.h"
#include "nRF5_evt.h"
#include "common/mible_beacon.h"
#include "secure_auth/mible_secure_auth.h"
#include "mijia_profiles/mi_service_server.h"
#include "mijia_profiles/lock_service_server.h"
#include "mi_config.h"

#define DEVICE_NAME                     "Nordic_Template"                       /**< Name of device. Will be included in the advertising data. */
#define MANUFACTURER_NAME               "NordicSemiconductor"                   /**< Manufacturer. Will be passed to Device Information Service. */

#define APP_BLE_OBSERVER_PRIO           3                                       /**< Application's BLE observer priority. You shouldn't need to modify this value. */
#define APP_BLE_CONN_CFG_TAG            1                                       /**< A tag identifying the SoftDevice BLE configuration. */

#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(15, UNIT_1_25_MS)        /**< Minimum acceptable connection interval (0.1 seconds). */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(60, UNIT_1_25_MS)        /**< Maximum acceptable connection interval (0.2 second). */
#define SLAVE_LATENCY                   0                                       /**< Slave latency. */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)         /**< Connection supervisory timeout (4 seconds). */

#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(30000)                   /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(60000)                  /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                       /**< Number of attempts before giving up the connection parameter negotiation. */

#define DEAD_BEEF                       0xDEADBEEF                              /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */


NRF_BLE_GATT_DEF(m_gatt);                                                       /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);                                                         /**< Context for the Queued Write module.*/
APP_TIMER_DEF(m_poll_timer);
APP_TIMER_DEF(m_bindconfirm_timer);

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;                        /**< Handle of the current connection. */
static int m_curr_times;
static int m_max_times;
/* YOUR_JOB: Declare all services structure your application is using
 *  BLE_XYZ_DEF(m_xyz);
 */

// YOUR_JOB: Use UUIDs for service(s) used in your application.
static void advertising_init(bool need_bind_confirm);
static void advertising_start(void);
static void poll_timer_handler(void * p_context);
static void bind_confirm_timeout(void * p_context);
static void ble_lock_ops_handler(uint8_t opcode);

/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num   Line number of the failing ASSERT call.
 * @param[in] file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}


/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
static void timers_init(void)
{
    // Initialize timer module.
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    // Create timers.

    /* YOUR_JOB: Create any timers to be used by the application.
                 Below is an example of how to create a timer.
                 For every new timer needed, increase the value of the macro APP_TIMER_MAX_TIMERS by
                 one.
       ret_code_t err_code;
       err_code = app_timer_create(&m_app_timer_id, APP_TIMER_MODE_REPEATED, timer_timeout_handler);
       APP_ERROR_CHECK(err_code); */
    err_code = app_timer_create(&m_poll_timer, APP_TIMER_MODE_REPEATED, poll_timer_handler);
    MI_ERR_CHECK(err_code);

    err_code = app_timer_create(&m_bindconfirm_timer, APP_TIMER_MODE_SINGLE_SHOT, bind_confirm_timeout);
    MI_ERR_CHECK(err_code);
}


/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void)
{
    ret_code_t              err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *)DEVICE_NAME,
                                          strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    /* YOUR_JOB: Use an appearance value matching the application's use case.
       err_code = sd_ble_gap_appearance_set(BLE_APPEARANCE_);
       APP_ERROR_CHECK(err_code); */

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the GATT module.
 */
static void gatt_init(void)
{
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for handling the YYY Service events.
 * YOUR_JOB implement a service handler function depending on the event the service you are using can generate
 *
 * @details This function will be called for all YY Service events which are passed to
 *          the application.
 *
 * @param[in]   p_yy_service   YY Service structure.
 * @param[in]   p_evt          Event received from the YY Service.
 *
 *
static void on_yys_evt(ble_yy_service_t     * p_yy_service,
                       ble_yy_service_evt_t * p_evt)
{
    switch (p_evt->evt_type)
    {
        case BLE_YY_NAME_EVT_WRITE:
            APPL_LOG("[APPL]: charact written with value %s. ", p_evt->params.char_xx.value.p_str);
            break;

        default:
            // No implementation needed.
            break;
    }
}
*/

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void)
{
    ret_code_t         err_code;
    nrf_ble_qwr_init_t qwr_init = {0};

    // Initialize Queued Write Module.
    qwr_init.error_handler = nrf_qwr_error_handler;

    err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
    APP_ERROR_CHECK(err_code);

    /* YOUR_JOB: Add code to initialize the services used by the application.
       ble_xxs_init_t                     xxs_init;
       ble_yys_init_t                     yys_init;

       // Initialize XXX Service.
       memset(&xxs_init, 0, sizeof(xxs_init));

       xxs_init.evt_handler                = NULL;
       xxs_init.is_xxx_notify_supported    = true;
       xxs_init.ble_xx_initial_value.level = 100;

       err_code = ble_bas_init(&m_xxs, &xxs_init);
       APP_ERROR_CHECK(err_code);

       // Initialize YYY Service.
       memset(&yys_init, 0, sizeof(yys_init));
       yys_init.evt_handler                  = on_yys_evt;
       yys_init.ble_yy_initial_value.counter = 0;

       err_code = ble_yy_service_init(&yys_init, &yy_init);
       APP_ERROR_CHECK(err_code);
     */
}


/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module which
 *          are passed to the application.
 *          @note All this function does is to disconnect. This could have been done by simply
 *                setting the disconnect_on_fail config parameter, but instead we use the event
 *                handler mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    ret_code_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}


/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
    ret_code_t             err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for starting timers.
 */
static void application_timers_start(void)
{
    /* YOUR_JOB: Start your timers. below is an example of how to start a timer.
       ret_code_t err_code;
       err_code = app_timer_start(m_app_timer_id, TIMER_INTERVAL, NULL);
       APP_ERROR_CHECK(err_code); */

//    ret_code_t err_code = app_timer_start(m_poll_timer, APP_TIMER_TICKS(10000), NULL);
//    MI_ERR_CHECK(err_code);
}


/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    ret_code_t err_code = NRF_SUCCESS;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("Disconnected.");
            // LED indication will be changed when advertising starts.
            break;

        case BLE_GAP_EVT_CONNECTED:
            NRF_LOG_INFO("Connected.");
            err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
            APP_ERROR_CHECK(err_code);
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        {
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys =
            {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            NRF_LOG_DEBUG("GATT Client Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server timeout event.
            NRF_LOG_DEBUG("GATT Server Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            // No implementation needed.
            break;
    }

    mible_on_ble_evt(p_ble_evt);

}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}


/**@brief Function for handling events from the BSP module.
 *
 * @param[in]   event   Event generated when button is pressed.
 */
static void bsp_event_handler(bsp_event_t event)
{
    static uint8_t lock_stat = 0;
    ret_code_t err_code;

    switch (event)
    {
        case BSP_EVENT_DISCONNECT:
            err_code = sd_ble_gap_disconnect(m_conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if (err_code != NRF_ERROR_INVALID_STATE)
            {
                APP_ERROR_CHECK(err_code);
            }
            break; // BSP_EVENT_DISCONNECT

		case BSP_EVENT_RESET:
			mi_scheduler_start(SYS_KEY_DELETE);
            break;

        case BSP_EVENT_BOND:
            advertising_init(1);
            err_code = app_timer_start(m_bindconfirm_timer, APP_TIMER_TICKS(5000), NULL);
            MI_ERR_CHECK(err_code);
            break;

        case BSP_EVENT_KEY_1:
            if (get_mi_reg_stat()) {
                ble_lock_ops_handler(lock_stat);
                lock_stat ^= 1;
            } else {
                mi_scheduler_start(SYS_MSC_SELF_TEST);
            }
            break;

        case BSP_EVENT_KEY_0:
            if (get_mi_reg_stat()) {
                bsp_board_led_on(0);
                m_curr_times = 0;
                m_max_times  = 1000;
                MI_LOG_DEBUG("Enter lock event test mode (%d): adv new event every 5s, keep adv 3s with interval 100 ms.\n", m_max_times);
                app_timer_start(m_poll_timer, APP_TIMER_TICKS(5000), NULL);
            } else {
                advertising_init(1);
                err_code = app_timer_start(m_bindconfirm_timer, APP_TIMER_TICKS(5000), NULL);
                MI_ERR_CHECK(err_code);
            }
            break;

        case BSP_EVENT_KEY_3:
            bsp_board_led_on(0);
            m_curr_times = 0;
            m_max_times  = 10000;
            MI_LOG_DEBUG("Enter lock event test mode (%d): adv new event every 5s, keep adv 3s with interval 100 ms.\n", m_max_times);
            app_timer_start(m_poll_timer, APP_TIMER_TICKS(5000), NULL);
            break;

        case BSP_EVENT_CLEAR_ALERT:
            bsp_board_led_off(0);
            MI_LOG_DEBUG("Exit lock event test mode.\n");
            app_timer_stop(m_poll_timer);
            break;

        default:
            break;
    }
}


/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(bool need_bind_confirm)
{
    MI_LOG_INFO("advertising init...\n");
	mibeacon_frame_ctrl_t frame_ctrl = {
		.secure_auth    = 1,
		.version        = 4,
        .bond_confirm   = need_bind_confirm,
	};

	mibeacon_capability_t cap = {.connectable = 1,
	                             .encryptable = 1,
	                             .bondAbility = 1};
    mibeacon_cap_sub_io_t IO = {.in_digits = 1};
	mible_addr_t dev_mac;
	mible_gap_address_get(dev_mac);

	mibeacon_config_t mibeacon_cfg = {
		.frame_ctrl = frame_ctrl,
		.pid = PRODUCT_ID,
		.p_mac = (mible_addr_t*)dev_mac, 
		.p_capability = &cap,
        .p_cap_sub_IO = &IO,
		.p_obj = NULL,
	};

	uint8_t adv_data[31];
    uint8_t adv_len;

    if (get_mi_reg_stat()) {
        mibeacon_cfg.frame_ctrl.version = 4;
        mibeacon_cfg.p_cap_sub_IO = 0;
    }

    // ADV Struct: Flags: LE General Discoverable Mode + BR/EDR Not supported.
	adv_data[0] = 0x02;
	adv_data[1] = 0x01;
	adv_data[2] = 0x06;
	adv_len     = 3;
	
	uint8_t service_data_len;
	if(MI_SUCCESS != mible_service_data_set(&mibeacon_cfg, adv_data+3, &service_data_len)){
		MI_LOG_ERROR("encode service data failed. \r\n");
		return;
	}
    adv_len += service_data_len;
	
	MI_LOG_HEXDUMP(adv_data, adv_len);

	mible_gap_adv_data_set(adv_data, adv_len, NULL, 0);

	return;
}

/**@brief Function for initializing buttons and leds.
 *
 * @param[out] p_erase_bonds  Will be true if the clear bonding button was pressed to wake the application up.
 */
static void buttons_leds_init(bool * p_erase_bonds)
{
    ret_code_t err_code;
//    bsp_event_t startup_event;

    err_code = bsp_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS, bsp_event_handler);
    APP_ERROR_CHECK(err_code);

//    err_code = bsp_btn_ble_init(NULL, &startup_event);
//    APP_ERROR_CHECK(err_code);

//    *p_erase_bonds = (startup_event == BSP_EVENT_CLEAR_BONDING_DATA);

    err_code = bsp_event_to_button_action_assign(0,
											 BSP_BUTTON_ACTION_PUSH,
											 BSP_EVENT_KEY_0);
    APP_ERROR_CHECK(err_code);

    err_code = bsp_event_to_button_action_assign(0,
											 BSP_BUTTON_ACTION_LONG_PUSH,
											 BSP_EVENT_CLEAR_ALERT);
    APP_ERROR_CHECK(err_code);


	/* assign BUTTON 2 to initate MSC_SELF_TEST, for more details to check bsp_event_handler()*/
    err_code = bsp_event_to_button_action_assign(1,
											 BSP_BUTTON_ACTION_PUSH,
											 BSP_EVENT_KEY_1);
    APP_ERROR_CHECK(err_code);

	/* assign BUTTON 3 to clear KEYINFO in the FLASH, for more details to check bsp_event_handler()*/
    err_code = bsp_event_to_button_action_assign(2,
											 BSP_BUTTON_ACTION_LONG_PUSH,
											 BSP_EVENT_RESET);
    APP_ERROR_CHECK(err_code);

	/* assign BUTTON 4 to set the bind confirm bit in mibeacon, for more details to check bsp_event_handler()*/
    err_code = bsp_event_to_button_action_assign(3,
											 BSP_BUTTON_ACTION_PUSH,
											 BSP_EVENT_KEY_3);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}


/**@brief Function for initializing power management.
 */
static void power_management_init(void)
{
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling the idle state (main loop).
 *
 * @details If there is no pending log operation, then sleep until next the next event occurs.
 */
static void idle_state_handle(void)
{
    if (NRF_LOG_PROCESS() == false)
    {
        nrf_pwr_mgmt_run();
    }
}


/**@brief Function for starting advertising.
 */
static void advertising_start(void)
{
	mible_gap_adv_param_t adv_param =(mible_gap_adv_param_t){
		.adv_type = MIBLE_ADV_TYPE_CONNECTABLE_UNDIRECTED, 
		.adv_interval_min = MSEC_TO_UNITS(100, UNIT_0_625_MS),
		.adv_interval_max = MSEC_TO_UNITS(100, UNIT_0_625_MS),
	};
    uint32_t err_code = mible_gap_adv_start(&adv_param);
    if(MI_SUCCESS != err_code){
		MI_LOG_ERROR("adv failed. %d \n", err_code);
	}
}


static void bind_confirm_timeout(void * p_context)
{
	MI_LOG_WARNING("bind confirm bit clear.\n");
    advertising_init(0);
}


static void poll_timer_handler(void * p_context)
{
	time_t utc_time = time(NULL);
	MI_LOG_INFO("The %d th event sent, %s", ++m_curr_times, ctime(&utc_time));
    ble_lock_ops_handler(0);
    
    if (m_curr_times > m_max_times) {
        app_timer_stop(m_poll_timer);
        m_curr_times = 0;
    }
//	uint8_t battery_stat = 100;
//	mibeacon_obj_enque(MI_STA_BATTERY, sizeof(battery_stat), &battery_stat);

}

void time_init(struct tm * time_ptr);

#define PAIRCODE_NUMS 6
bool need_kbd_input;
uint8_t pair_code_num;
uint8_t pair_code[PAIRCODE_NUMS];

int scan_keyboard(uint8_t *pdata, uint8_t len)
{
	if (pdata == NULL)
		return 0;

	return SEGGER_RTT_ReadNoLock(0, pdata, len);
}

void flush_keyboard_buffer(void)
{
    uint8_t tmp[16];
    while(SEGGER_RTT_ReadNoLock(0, tmp, 16));
}

void mi_schd_event_handler(schd_evt_t *p_event)
{
	MI_LOG_INFO("USER CUSTOM CALLBACK RECV EVT ID %d\n", p_event->id);

    if (p_event->id == SCHD_EVT_OOB_REQUEST) {
        need_kbd_input = true;
        flush_keyboard_buffer();
        MI_LOG_INFO(MI_LOG_COLOR_GREEN "Please input your pair code ( MUST be 6 digits ) : \n");
    } else {
        uint8_t did[8];
        get_mi_device_id(did);
        MI_LOG_INFO("device ID (hex):\n");
        MI_LOG_HEXDUMP(did, 8);
    }
        
}

#ifdef NRF52840_XXAA
#define MSC_PWR_PIN                     20
const iic_config_t iic_config = {
        .scl_pin  = 21,
        .sda_pin  = 22,
        .freq = IIC_100K
};
#else
#define MSC_PWR_PIN                     23
const iic_config_t iic_config = {
        .scl_pin  = 24,
        .sda_pin  = 25,
        .freq = IIC_400K
};
#endif


int mijia_secure_chip_power_manage(bool power_stat)
{
    if (power_stat == 1) {
        nrf_gpio_cfg_output(MSC_PWR_PIN);
        nrf_gpio_pin_set(MSC_PWR_PIN);
    } else {
        nrf_gpio_pin_clear(MSC_PWR_PIN);
    }
    return 0;
}


void ble_lock_ops_handler(uint8_t opcode)
{

    switch(opcode) {
    case 0:
        MI_LOG_INFO(" unlock \n");
        bsp_board_led_off(1);
        bsp_board_led_off(2);
        break;

    case 1:
        MI_LOG_INFO(" lock \n");
        bsp_board_led_on(1);
        break;

    case 2:
        MI_LOG_INFO(" bolt \n");
        bsp_board_led_on(2);
        break;

    default:
        MI_LOG_ERROR("lock opcode error %d", opcode);
    }

    lock_event_t lock_event;
    lock_event.action = opcode;
    lock_event.method = 0;
    lock_event.user_id= get_mi_key_id();
    lock_event.time   = time(NULL);

    mibeacon_obj_enque(MI_EVT_LOCK, sizeof(lock_event), &lock_event);
			
    reply_lock_stat(opcode);
    send_lock_log((uint8_t *)&lock_event, sizeof(lock_event));
}

/**@brief Function for application main entry.
 */
int main(void)
{
    bool erase_bonds;

    // Initialize.
    log_init();
    timers_init();
    MI_LOG_INFO(RTT_CTRL_CLEAR"Compiled  %s %s\n", (uint32_t)__DATE__, (uint32_t)__TIME__);
    buttons_leds_init(&erase_bonds);
    power_management_init();
    ble_stack_init();
    gap_params_init();
    gatt_init();
    advertising_init(0);
    services_init();
    conn_params_init();

    time_init(NULL);

    mible_libs_config_t config = {
        .msc_onoff        = mijia_secure_chip_power_manage,
        .p_msc_iic_config = (void*)&iic_config
    };

	/* <!> mi_scheduler_init() must be called after ble_stack_init(). */
    mi_service_init();
	mi_scheduler_init(10, mi_schd_event_handler, &config);
    mi_scheduler_start(SYS_KEY_RESTORE);

    lock_init_t lock_config;
    lock_config.opcode_handler = ble_lock_ops_handler;
    lock_service_init(&lock_config);

    // Start execution.
    application_timers_start();
    advertising_start();

    // Enter main loop.
    for (;;) {

        if (need_kbd_input) {
            if (pair_code_num < PAIRCODE_NUMS) {
                pair_code_num += scan_keyboard(pair_code + pair_code_num, PAIRCODE_NUMS - pair_code_num);
            }
            if (pair_code_num == PAIRCODE_NUMS) {
                pair_code_num = 0;
                need_kbd_input = false;
                mi_input_oob(pair_code, sizeof(pair_code));
            }
        }

#if (MI_SCHD_PROCESS_IN_MAIN_LOOP==1)
        mi_schd_process();
#endif
        idle_state_handle();
    }
}


/**
 * @}
 */
