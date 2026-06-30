import flask

import qrcode

def generate_qr_localhost(data1: str, data2: str, 
                          host: str = "localhost", 
                          port: int = 80,
                          filename: str = "qrcode.png"):
    # Формируем URL
    if port == 80:
        url = f"http://{host}/{data1}/{data2}"
    else:
        url = f"http://{host}:{port}/{data1}/{data2}"

    # Генерируем QR
    qr = qrcode.QRCode(
        version=1,
        error_correction=qrcode.constants.ERROR_CORRECT_M,
        box_size=10,
        border=4,
    )
    qr.add_data(url)
    qr.make(fit=True)

    img = qr.make_image(fill_color="black", back_color="white")
    img.save(filename)
    print(f"QR-код сохранён в «{filename}» → {url}")
    return url