#define usb_puts

#IFNDEF __USB_DRIVER__
#DEFINE __USB_DRIVER__

#include "usb.h"

#if defined(__PIC16_USB_H__)
 #include "pic_usb.c"
#endif

#if defined(__PIC18_USB_H__)
 #include "pic18_usb.c"
#endif

#if defined(__PIC24_USB_H__)
 #include "pic24_usb.c"
#endif

#if defined(__USBN960X_H__)
 #include "usbn960x.c"
#endif

#define usb_puts   puts

#IFNDEF __USB_HARDWARE__
   #ERROR You must include USB hardware driver.
#ENDIF

#IFNDEF __USB_DESCRIPTORS__
   #ERROR You must include USB descriptors.
#ENDIF

TYPE_USB_STACK_STATUS USB_stack_status;

int8 USB_address_pending;                        //save previous state because packets can take several isrs
int16 usb_getdesc_ptr; 
unsigned int16 usb_getdesc_len=0;             //for reading string and config descriptors

#IF USB_HID_BOOT_PROTOCOL
int8 hid_protocol[USB_NUM_HID_INTERFACES];
#ENDIF

void usb_put_0len_0(void);
void usb_match_registers(int8 endpoint, int16 *status, int16 *buffer, int8 *size);

void usb_isr_tkn_setup_StandardEndpoint(void);
void usb_isr_tkn_setup_StandardDevice(void);
void usb_isr_tkn_setup_StandardInterface(void);
void usb_isr_tkn_setup_ClassInterface(void);
void usb_Get_Descriptor(void);
void usb_copy_desc_seg_to_ep(void);
void usb_finish_set_address(void);

int8 USB_Interface[USB_MAX_NUM_INTERFACES];              //config state for all of our interfaces, NUM_INTERFACES defined with descriptors

/// BEGIN User Functions

// see usb.h for documentation
int1 usb_enumerated(void)
{
   return(USB_stack_status.curr_config);
}

// see usb.h for documentation
void usb_wait_for_enumeration(void) 
{
   while (USB_stack_status.curr_config == 0) {restart_wdt();}
}

// see USB.H for documentation
int1 usb_puts(int8 endpoint, int8 * ptr, unsigned int16 len, unsigned int8 timeout) {
   unsigned int16 i=0;
   int1 res;
   unsigned int16 this_packet_len;
   unsigned int16 packet_size;
   unsigned int32 timeout_1us;

   packet_size = usb_ep_tx_size[endpoint];
   
   //printf("\r\nUSB PUTS %U LEN=%LU MAX_PACK=%LU\r\n", endpoint, len, packet_size);

   //send data packets until timeout or no more packets to send
   while (i < len) 
   {
      timeout_1us = (int32)timeout*1000;
      if ((len - i) > packet_size) {this_packet_len = packet_size;}
      else {this_packet_len = len-i;}
      //putc('*');
      do 
      {
         res = usb_put_packet(endpoint, ptr + i, this_packet_len, USB_DTS_TOGGLE);   //send 64 byte packets
         //putc('.');
         if (!res)
         {
            delay_us(1);
            //delay_ms(500);
            timeout_1us--;
         }
      } while (!res && timeout_1us);
      i += packet_size;
   }


   //send 0len packet if needed
   if (i==len) {
      timeout_1us=(int32)timeout*1000;
      do {
         res = usb_put_packet(endpoint,0,0,USB_DTS_TOGGLE);   //indicate end of message
         if (!res) {
            delay_us(1);
            timeout_1us--;
         }
      } while (!res && timeout_1us);
   }

   return(res);
}

// see usb.h for documentation
unsigned int16 usb_gets(int8 endpoint, int8 * ptr, unsigned int16 max, unsigned int16 timeout) {
   unsigned int16 ret=0;
   unsigned int16 to;
   unsigned int16 len;
   unsigned int16 packet_size;
   unsigned int16 this_packet_max;

   packet_size=usb_ep_rx_size[endpoint];

   do {
      if (packet_size < max) {this_packet_max=packet_size;} else {this_packet_max=max;}
      to=0;
      do {
         if (usb_kbhit(endpoint)) {
            len=usb_get_packet(endpoint,ptr,this_packet_max);
            ptr+=len;
            max-=len;
            ret+=len;
            break;
         }
         else {
            to++;
            delay_ms(1);
         }
      } while (to!=timeout);
   } while ((len == packet_size) && (to!=timeout) && max);

   return(ret);
}

/// END User Functions


/// BEGIN USB Token, standard and HID request handler (part of ISR)

// see usb.h for documentation
void usb_token_reset(void) 
{
   unsigned int i;

   for (i=0;i<USB_MAX_NUM_INTERFACES;i++) 
      USB_Interface[i] = 0;   //reset each interface to default

  #IF USB_HID_BOOT_PROTOCOL
   for (i=0;i<USB_NUM_HID_INTERFACES; i++)
      hid_protocol[i] = 1;
  #endif

  #if USB_CDC_DEVICE
   usb_cdc_init();
  #endif

   USB_stack_status.curr_config = 0;      //unconfigured device

   USB_stack_status.status_device = 1;    //previous state.  init at none
   USB_stack_status.dev_req = NONE;       //previous token request state.  init at none
}

//send a 0len packet to endpoint 0 (optimization)
//notice that this doesnt return the status
#define usb_put_0len_0() usb_request_send_response(0)

/**************************************************************
/* usb_endpoint_is_valid(endpoint)
/*
/* Input: endpoint - endpoint to check.
/*                   bit 7 is direction (set is IN, clear is OUT)
/*
/* Output: TRUE if endpoint is valid, FALSE if not
/*
/* Summary: Checks the dynamic configuration to see if requested
/*          endpoint is a valid endpoint.
/***************************************************************/
int1 usb_endpoint_is_valid(int8 endpoint) 
{
   int1 direction;
   
   direction = bit_test(endpoint,7);
   
   endpoint &= 0x7F;
   
   if (endpoint > 16)
      return(FALSE);
   
   if (direction) { //IN
      return(usb_ep_tx_type[endpoint] != USB_ENABLE_DISABLED);
   }
   else {   //OUT
      return(usb_ep_rx_type[endpoint] != USB_ENABLE_DISABLED);
   }
}

// see usb.h for documentation
void usb_isr_tok_in_dne(int8 endpoint) {
   if (endpoint==0) {
      if (USB_stack_status.dev_req == GET_DESCRIPTOR) {usb_copy_desc_seg_to_ep();} //check this, we are missing report descriptor?
      else if (USB_stack_status.dev_req == SET_ADDRESS) {usb_finish_set_address();}
   }
  #if USB_CDC_DEVICE
  else if (endpoint==USB_CDC_DATA_IN_ENDPOINT) { //see ex_usb_serial.c example and usb_cdc.h driver
      usb_isr_tok_in_cdc_data_dne();
  }
  #endif
}

// see usb.h for documentation
void usb_isr_tok_out_dne(int8 endpoint)
{
   //TODO:
   if (endpoint==0) {
     #if USB_CDC_DEVICE
      usb_isr_tok_out_cdc_control_dne();
     #endif
   }
  #if USB_CDC_DEVICE
   else if (endpoint==USB_CDC_DATA_OUT_ENDPOINT) { //see ex_usb_serial.c example and usb_cdc.h driver
      usb_isr_tok_out_cdc_data_dne();
   }
  #endif
   //else {
   //   bit_set(__usb_kbhit_status,endpoint);
   //}
}


//---- process setup message stage -----------//
void Hub_SetFeature(unsigned char feature,unsigned char port);
void Hub_ClearFeature(unsigned char feature,unsigned char port);
void Hub_GetStatus(unsigned char port,unsigned char *buf);
void OnDongleOK();

// see usb.h for documentation
void usb_isr_tok_setup_dne(void) 
{
   USB_stack_status.dev_req=NONE; // clear the device request..

   switch(usb_ep0_rx_buffer[0] & 0x7F) {

      case 0x00:  //standard to device
         usb_isr_tkn_setup_StandardDevice();
         break;

      case 0x01:  //standard to interface
         usb_isr_tkn_setup_StandardInterface();
         break;

      case 0x02:  //standard to endpoint
         usb_isr_tkn_setup_StandardEndpoint();
         break;

      case 0x20:  //class specific request.  the only class this driver supports is HID
         usb_isr_tkn_setup_ClassInterface();
         break;

     case 0x21:
         usb_put_0len_0();
         break;
         
     case 0x23:   //HUB
        if(usb_ep0_rx_buffer[1]==3)
        {
              Hub_SetFeature(usb_ep0_rx_buffer[2],usb_ep0_rx_buffer[4]);
            usb_put_0len_0();
        }
        else if(usb_ep0_rx_buffer[1]==1)
        {
              Hub_ClearFeature(usb_ep0_rx_buffer[2],usb_ep0_rx_buffer[4]);
            usb_put_0len_0();
        }
        else if(usb_ep0_rx_buffer[1]==0)
        {
         Hub_GetStatus(usb_ep0_rx_buffer[4],usb_ep0_tx_buffer);
            usb_request_send_response(4);
        }
        break;
        
      default:
         usb_request_stall();
         break;
   }
}

/**************************************************************
/* usb_isr_tkn_setup_StandardDevice()
/*
/* Input: usb_ep0_rx_buffer[1] == bRequest
/*
/* Summary: bmRequestType told us it was a Standard Device request.
/*          bRequest says which request.  Only certain requests are valid,
/*          if a non-valid request was made then return with an Wrong-Statue (IDLE)
/*
/* Part of usb_isr_tok_setup_dne()
/***************************************************************/
void usb_isr_tkn_setup_StandardDevice(void) {
   switch(usb_ep0_rx_buffer[1]) {

      case USB_STANDARD_REQUEST_GET_STATUS:  //0
            usb_ep0_tx_buffer[0]=USB_stack_status.status_device;
            usb_ep0_tx_buffer[1]=0;
            usb_request_send_response(2);
            break;

      case USB_STANDARD_REQUEST_CLEAR_FEATURE:  //1
            if (usb_ep0_rx_buffer[2] == 1) {
               USB_stack_status.status_device &= 1;
               usb_put_0len_0();
            }
            else
               usb_request_stall();
            break;

      case USB_STANDARD_REQUEST_SET_FEATURE: //3
            if (usb_ep0_rx_buffer[2] == 1) {
               USB_stack_status.status_device |= 2;
               usb_put_0len_0();
            }
            else
               usb_request_stall();
            break;

      case USB_STANDARD_REQUEST_SET_ADDRESS: //5
            USB_stack_status.dev_req=SET_ADDRESS; //currently processing set_address request
            USB_address_pending=usb_ep0_rx_buffer[2];
            #ifdef __USBN__   //NATIONAL part handles this differently than pic16c7x5
            USB_stack_status.dev_req=NONE; //currently processing set_address request
            usb_set_address(USB_address_pending);
            USB_stack_status.curr_config=0;   // make sure current configuration is 0
            #endif
            usb_put_0len_0();
            break;

      case USB_STANDARD_REQUEST_GET_DESCRIPTOR: //6
            usb_Get_Descriptor();
            break;

      case USB_STANDARD_REQUEST_GET_CONFIGURATION: //8
            usb_ep0_tx_buffer[0]=USB_stack_status.curr_config;
            usb_request_send_response(1);
            break;

      case USB_STANDARD_REQUEST_SET_CONFIGURATION: //9
            if (usb_ep0_rx_buffer[2] <= USB_NUM_CONFIGURATIONS) {
               USB_stack_status.curr_config=usb_ep0_rx_buffer[2];
               usb_set_configured(usb_ep0_rx_buffer[2]);          
               usb_put_0len_0();
            }
            break;

      default:
            usb_request_stall();
            break;
   }
}

/**************************************************************
/* usb_isr_tkn_setup_StandardInterface()
/*
/* Input: usb_ep0_rx_buffer[1] == bRequest
/*
/* Summary: bmRequestType told us it was a Standard Interface request.
/*          bRequest says which request.  Only certain requests are valid,
/*          if a non-valid request was made then return with an Wrong-Statue (IDLE)
/*
/* Part of usb_isr_tok_setup_dne()
/***************************************************************/
void OnInterfaceSet();
void usb_isr_tkn_setup_StandardInterface(void) {
   int8 curr_config;

   curr_config=USB_stack_status.curr_config;

   //printf("%X",usb_ep0_rx_buffer[1]);

   switch (usb_ep0_rx_buffer[1]) {
      case USB_STANDARD_REQUEST_GET_STATUS:
            usb_ep0_tx_buffer[0]=0;
            usb_ep0_tx_buffer[1]=0;
            usb_request_send_response(2);
            break;

      case USB_STANDARD_REQUEST_GET_INTERFACE:
            if ( curr_config && (usb_ep0_rx_buffer[4] < USB_NUM_INTERFACES[curr_config-1]) ) {   //book says only supports configed state
               usb_ep0_tx_buffer[0]=USB_Interface[usb_ep0_rx_buffer[4]];//our new outgoing byte
               usb_request_send_response(1);; //send byte back
            }
            else
               usb_request_stall();
            break;

      case USB_STANDARD_REQUEST_SET_INTERFACE:
            usb_put_0len_0();
            break;

#IF USB_HID_DEVICE
      case USB_STANDARD_REQUEST_GET_DESCRIPTOR:
            usb_Get_Descriptor();
            break;
#endif
      default:
            usb_request_stall();
            break;
   }
}

/**************************************************************
/* usb_isr_tkn_setup_StandardEndpoint()
/*
/* Input: usb_ep0_rx_buffer[1] == bRequest
/*
/* Summary: bmRequestType told us it was a Standard Endpoint request.
/*          bRequest says which request.  Only certain requests are valid,
/*          if a non-valid request was made then return with an Wrong-Statue (IDLE)
/*
/* Part of usb_isr_tok_setup_dne()
/***************************************************************/
void usb_isr_tkn_setup_StandardEndpoint(void) {
   if (usb_endpoint_is_valid(usb_ep0_rx_buffer[4])) {
      switch(usb_ep0_rx_buffer[1]) {

         case USB_STANDARD_REQUEST_CLEAR_FEATURE:
               usb_unstall_ep(usb_ep0_rx_buffer[4]);
               usb_put_0len_0();
               break;

         case USB_STANDARD_REQUEST_SET_FEATURE:
                     usb_stall_ep(usb_ep0_rx_buffer[4]);
                     usb_put_0len_0();
                     break;

         case USB_STANDARD_REQUEST_GET_STATUS:
               usb_ep0_tx_buffer[0]=0;
               usb_ep0_tx_buffer[1]=0;
               if (usb_endpoint_stalled(usb_ep0_rx_buffer[4])) {
                  usb_ep0_tx_buffer[0]=1;
               }
               usb_request_send_response(2);
               break;

         default:
            usb_request_stall();
            break;
      }
   }
}

/**************************************************************
/* usb_isr_tkn_setup_ClassInterface()
/*
/* Input: usb_ep0_rx_buffer[1] == bRequest
/*
/* Summary: bmRequestType told us it was a Class request.  The only Class this drivers supports is HID.
/*          bRequest says which request.  Only certain requests are valid,
/*          if a non-valid request was made then return with an Wrong-Statue (IDLE)
/*
/* Part of usb_isr_tok_setup_dne()
/* Only compiled if HID_DEVICE is TRUE
/***************************************************************/
void usb_isr_tkn_setup_ClassInterface(void) {
   switch(usb_ep0_rx_buffer[1]) 
   {
      case USB_STANDARD_REQUEST_GET_DESCRIPTOR: //6
            usb_Get_Descriptor();
            break;

      default:
            usb_request_stall();
            break;
   }
}

/**************************************************************
/* usb_Get_Descriptor()
/*
/* Input: usb_ep0_rx_buffer[3] == wValue, which descriptor we want
/*        usb_ep0_rx_buffer[6,7] == Max length the host will accept
/*
/* Summary: Checks to see if we want a standard descriptor (Interface, Endpoint, Config, Device, String, etc.),
/*          or a class specific (HID) descriptor.  Since some pics (especially the PIC167x5) doesn't have
/*          pointers to constants we must simulate or own by setting up global registers that say
/*          which constant array to deal with, which position to start in this array, and the length.
/*          Once these globals are setup the first packet is sent.  If a descriptor takes more than one packet
/*          the PC will send an IN request to endpoint 0, and this will be handled by usb_isr_tok_in_dne()
/*          which will send the rest of the data.
/*
/* Part of usb_isr_tok_setup_dne()
/***************************************************************/

int16 GetDevicePointer();
int16 GetHubPointer();
int16 GetConfigPointer(unsigned char nConfig,char shortConfig);
int16 GetStringPointer(unsigned char nString);
int16 GetDeviceLength();
int16 GetHubLength();
int16 GetConfigLength(unsigned char nConfig,char shortConfig);
int16 GetStringLength(unsigned char nString);



void usb_Get_Descriptor() 
{
   usb_getdesc_ptr=0;
   USB_stack_status.getdesc_type=USB_GETDESC_CONFIG_TYPE;

   switch(usb_ep0_rx_buffer[3]) 
   {
      case USB_DESC_DEVICE_TYPE:    //1
         usb_getdesc_ptr=GetDevicePointer();
            usb_getdesc_len=GetDeviceLength();
            USB_stack_status.getdesc_type=USB_GETDESC_DEVICE_TYPE;
            break;
      case 0x29:    //1
         usb_getdesc_ptr=GetHubPointer();
            usb_getdesc_len=GetHubLength();
            USB_stack_status.getdesc_type=USB_GETDESC_DEVICE_TYPE;
            break;

      //windows hosts will send a FF max len and expect you to send all configs without asking for them individually.
      case USB_DESC_CONFIG_TYPE:   //2
         if(usb_ep0_rx_buffer[6]==8 && usb_ep0_rx_buffer[7]==0)
         {
            usb_getdesc_ptr=GetConfigPointer(usb_ep0_rx_buffer[2],1);
            usb_getdesc_len=GetConfigLength(usb_ep0_rx_buffer[2],1);
         }
         else
         {
            usb_getdesc_ptr=GetConfigPointer(usb_ep0_rx_buffer[2],0);
            usb_getdesc_len=GetConfigLength(usb_ep0_rx_buffer[2],0);
         }
            
            break;

      case USB_DESC_STRING_TYPE: //3
            USB_stack_status.getdesc_type=USB_GETDESC_STRING_TYPE;
            usb_getdesc_ptr=GetStringPointer(usb_ep0_rx_buffer[2]);
            usb_getdesc_len=GetStringLength(usb_getdesc_ptr);
            break;


      default:
            usb_request_stall();
            return;
   }
//   printf("SZ %X %X",usb_ep0_rx_buffer[7],usb_ep0_rx_buffer[6]);
   if (usb_ep0_rx_buffer[7]==0) {
      if (usb_getdesc_len > usb_ep0_rx_buffer[6])
         usb_getdesc_len = usb_ep0_rx_buffer[6];
   }
   USB_stack_status.dev_req=GET_DESCRIPTOR;
   usb_copy_desc_seg_to_ep();
}

/**************************************************************
/* usb_finish_set_address()
/*
/* Input: USB_address_pending holds the address we were asked to set to.
/*
/* Summary: Sets the address.
/*
/* This code should only be run on the PIC USB peripheral, and not the
/* National peripheral.
/*
/* Part of usb_isr_tok_setup_dne()
/***************************************************************/
 void usb_finish_set_address() {
   USB_stack_status.curr_config=0;   // make sure current configuration is 0

   #ifdef __PIC__
   USB_stack_status.dev_req=NONE;  // no request pending
   usb_set_address(USB_address_pending);
   #endif
}

////////////////////////////////////////////////////////////////////////////
///
/// The following function retrieve data from constant arrays.  This may
/// look un-optimized, but remember that you can't create a pointer to
/// a constant array.
///
///////////////////////////////////////////////////////////////////////////
void usb_copy_desc_seg_to_ep(void) {
   unsigned int i=0;
   char c;

   while ((usb_getdesc_len)&&(i<USB_MAX_EP0_PACKET_LENGTH))
   {
      switch(USB_stack_status.getdesc_type) {
         case USB_GETDESC_CONFIG_TYPE:
            c=USB_CONFIG_DESC[usb_getdesc_ptr];
            break;

         case USB_GETDESC_STRING_TYPE:
            c=USB_STRING_DESC[usb_getdesc_ptr];
            break;

         case USB_GETDESC_DEVICE_TYPE:
            c=USB_DEVICE_DESC[usb_getdesc_ptr];
            break;
      }
      usb_getdesc_ptr++;
      usb_getdesc_len--;
      usb_ep0_tx_buffer[i++]=c;
   }

   if ((!usb_getdesc_len)&&(i!=USB_MAX_EP0_PACKET_LENGTH)) {
         USB_stack_status.dev_req = NONE;
   }

   usb_request_send_response(i);
}
#ENDIF
