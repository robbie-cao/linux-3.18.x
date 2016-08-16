#ifndef __LINUX_USB_PCI_QUIRKS_H
#define __LINUX_USB_PCI_QUIRKS_H

#if defined (CONFIG_PCI) && !defined (CONFIG_USB_MT7621_XHCI_PLATFORM)
void uhci_reset_hc(struct pci_dev *pdev, unsigned long base);
int uhci_check_and_reset_hc(struct pci_dev *pdev, unsigned long base);
#endif  /* CONFIG_PCI */

#if defined(CONFIG_PCI) && !defined(CONFIG_PCI_DISABLE_COMMON_QUIRKS)
int usb_amd_find_chipset_info(void);
int usb_hcd_amd_remote_wakeup_quirk(struct pci_dev *pdev);
bool usb_amd_hang_symptom_quirk(void);
bool usb_amd_prefetch_quirk(void);
void usb_amd_dev_put(void);
void usb_amd_quirk_pll_disable(void);
void usb_amd_quirk_pll_enable(void);
void usb_enable_intel_xhci_ports(struct pci_dev *xhci_pdev);
void usb_disable_xhci_ports(struct pci_dev *xhci_pdev);
void sb800_prefetch(struct device *dev, int on);
#else
struct pci_dev;
static inline int usb_amd_find_chipset_info(void)
{
	return 0;
}
static inline bool usb_amd_hang_symptom_quirk(void)
{
	return false;
}
static inline bool usb_amd_prefetch_quirk(void)
{
	return false;
}
static inline void usb_amd_quirk_pll_disable(void) {}
static inline void usb_amd_quirk_pll_enable(void) {}
static inline void usb_amd_dev_put(void) {}
static inline void usb_disable_xhci_ports(struct pci_dev *xhci_pdev) {}
static inline void sb800_prefetch(struct device *dev, int on) {}
static inline void usb_enable_intel_xhci_ports(struct pci_dev *xhci_pdev) {}
#endif

#endif  /*  __LINUX_USB_PCI_QUIRKS_H  */
