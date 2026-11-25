#include "usb_msc_gate.hpp"
#include "tusb.h"
namespace tab5::usb {
static MscIoGate gate;
void allow_host_io(bool on){gate.allow(on);}
MscIoSnapshot msc_io_snapshot(){return gate.snapshot();}
}
extern "C" {
int32_t __real_tud_msc_read10_cb(uint8_t,uint32_t,uint32_t,void *,uint32_t);
int32_t __real_tud_msc_write10_cb(uint8_t,uint32_t,uint32_t,uint8_t *,uint32_t);
bool __real_tud_msc_test_unit_ready_cb(uint8_t);
static void no_medium(uint8_t lun){
  tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0);
}
int32_t __wrap_tud_msc_read10_cb(uint8_t lun,uint32_t lba,uint32_t offset,void *buffer,uint32_t size){
  bool dispatched=false;
  const auto n=tab5::usb::gate.run(tab5::usb::HostIo::Read,[&]{
    dispatched=true;return __real_tud_msc_read10_cb(lun,lba,offset,buffer,size);
  });
  if(!dispatched)no_medium(lun);
  return n;
}
int32_t __wrap_tud_msc_write10_cb(uint8_t lun,uint32_t lba,uint32_t offset,uint8_t *buffer,uint32_t size){
  bool dispatched=false;
  const auto n=tab5::usb::gate.run(tab5::usb::HostIo::Write,[&]{
    dispatched=true;return __real_tud_msc_write10_cb(lun,lba,offset,buffer,size);
  });
  if(!dispatched)no_medium(lun);
  return n;
}
bool __wrap_tud_msc_test_unit_ready_cb(uint8_t lun){
  bool dispatched=false;
  const auto n=tab5::usb::gate.run(tab5::usb::HostIo::Ready,[&]{
    dispatched=true;return __real_tud_msc_test_unit_ready_cb(lun)?1:0;
  });
  if(!dispatched)no_medium(lun);
  return n>0;
}
}
