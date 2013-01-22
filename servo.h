
#include <asm/ioctl.h>

#define IOC_MOTOR_MAGIC  'K'
/* input */
#define MOTOR_ENABLE		_IO(IOC_MOTOR_MAGIC, 10) //dec: 19210
#define MOTOR_DIR			_IO(IOC_MOTOR_MAGIC, 11)
#define MOTOR_PWM_ON		_IO(IOC_MOTOR_MAGIC, 12)
#define MOTOR_PWM_OFF		_IO(IOC_MOTOR_MAGIC, 13)
#define MOTOR_PWM_SET		_IO(IOC_MOTOR_MAGIC, 14)
#define MOTOR_RESET			_IO(IOC_MOTOR_MAGIC, 15)
#define MOTOR_STEPS			_IO(IOC_MOTOR_MAGIC, 16)
#define MOTOR_START			_IO(IOC_MOTOR_MAGIC, 17)
#define MOTOR_LOWPWR		_IO(IOC_MOTOR_MAGIC, 18)

/* output */
#define MOTOR_TO_END		_IO(IOC_MOTOR_MAGIC, 30)	//dec: 19230
