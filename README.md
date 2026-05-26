# Đồ án Hệ thống Nhúng: Hệ thống học tập bằng thẻ cho giáo dục sớm

## Tóm tắt dự án
Dự án là một thiết bị giáo dục thông minh ứng dụng công nghệ IoT, được thiết kế nhằm giúp trẻ em học chữ cái, từ vựng và giải trí một cách trực quan, sinh động. Thông qua việc tương tác vật lý với thẻ RFID, hệ thống cung cấp các phản hồi tức thì bằng hình ảnh và âm thanh chất lượng cao, giúp trẻ học tập chủ động và tăng cường khả năng ghi nhớ.

## Công nghệ & Phần cứng cốt lõi
* **Vi điều khiển:** ESP32-WROOM-32 (Xử lý đa luồng).
* **Hệ điều hành:** FreeRTOS (Quản lý đa tác vụ `Tasks` và hàng đợi `Queues`).
* **Module giao tiếp:** Cảm biến đọc thẻ RFID RC522 (SPI).
* **Hiển thị & Âm thanh:** Màn hình TFT LCD ST7789 (SPI) & IC Khuếch đại MAX98357A (I2S).
* **Cơ sở dữ liệu đám mây:** Firebase Realtime Database (Lưu trữ lịch sử học tập qua Wi-Fi).

## Tính năng nổi bật
1. **Chế độ học (Learning Mode):** Trẻ quét thẻ từ vựng, hệ thống phát âm thanh (từ thẻ nhớ SD) và hiển thị hình ảnh tương ứng.
2. **Chế độ kiểm tra (Test Mode):** Đưa ra câu hỏi ngẫu nhiên với giới hạn thời gian 10s. Chấm điểm và phản hồi trực quan (Đúng/Sai).
3. **Chế độ giải trí (Music Mode):** Phát nhạc nền hoặc kể chuyện.
4. **Đồng bộ IoT:** Ghi nhận và đẩy log thao tác, điểm số lên hệ thống Firebase theo thời gian thực.
5. **Cấu hình mạng linh hoạt:** Hỗ trợ Captive Portal (AP Mode) để cài đặt Wi-Fi và lưu trữ cấu hình qua EEPROM.
