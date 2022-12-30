LÉAME.md
Proxygen: Bibliotecas C++ HTTP de Facebook
Apoya a Ucrania Estado de compilación de Linux Estado de compilación de macOS

Este proyecto comprende las abstracciones principales de C++ HTTP utilizadas en Facebook. Internamente, se utiliza como base para construir muchos servidores HTTP, proxies y clientes. Esta versión se centra en las abstracciones HTTP comunes y en nuestro marco HTTPServer simple. Las versiones futuras también proporcionarán API de cliente simples. El marco admite HTTP/1.1, SPDY/3, SPDY/3.1, HTTP/2 y HTTP/3. El objetivo es proporcionar una biblioteca HTTP de C++ simple, eficaz y moderna.

Tenemos un grupo de Google para debates generales en https://groups.google.com/d/forum/facebook-proxygen .

La publicación de blog original también tiene más información sobre el proyecto.

Obtenga más información en este video de introducción
Explique como si tuviera 5 años: Proxygen

Instalando
Tenga en cuenta que actualmente este proyecto ha sido probado en Ubuntu 18.04 y Mac OSX, aunque probablemente funcione en muchas otras plataformas.

Necesitará al menos 3 GiB de memoria para compilar proxygeny sus dependencias.

Instalación fácil
Simplemente ejecute ./build.shdesde el proxygen/directorio para obtener y compilar todas las dependencias y proxygen. Puede ejecutar las pruebas manualmente con cd _build/ && make test. Luego ejecuta ./install.shpara instalarlo. Puede eliminar el directorio de compilación temporal ( _build) y ./build.sh && ./install.sh volver a establecer las dependencias, y luego reconstruir y reinstalar proxygen.

Otras plataformas
Si está ejecutando en otra plataforma, es posible que primero deba instalar varios paquetes. Proxygen y follyson todos proyectos basados ​​en Autotools.

Introducción
Estructura y contenido del directorio:

Directorio	Objetivo
proxygen/external/	Contiene código de terceros no instalado del que depende proxygen.
proxygen/lib/	Abstracciones de redes centrales.
proxygen/lib/http/	Código específico de HTTP. (incluyendo HTTP/2 y HTTP/3)
proxygen/lib/services/	Gestión de conexiones y código de servidor.
proxygen/lib/utils/	Código auxiliar misceláneo.
proxygen/httpserver/	Contiene envoltura de código proxygen/lib/para construir servidores http C++ simples. Recomendamos construir sobre estas API.
Arquitectura
Las abstracciones centrales a comprender proxygen/libson la sesión, el códec, la transacción y el controlador. Estas son las abstracciones de nivel más bajo y, por lo general, no recomendamos construir directamente a partir de ellas.

Cuando los bytes se leen del cable, el HTTPCodecinterior almacenado los HTTPSessionanaliza en objetos de nivel superior y los asocia con un identificador de transacción. Luego, el códec llama al HTTPSessionque es responsable de mantener el mapeo entre el identificador de transacción y los HTTPTransactionobjetos. Cada par de solicitud/respuesta HTTP tiene un HTTPTransactionobjeto separado. Finalmente, HTTPTransactionreenvía la llamada a un objeto controlador que implementa HTTPTransaction:: Handler. El controlador es responsable de implementar la lógica comercial para la solicitud o respuesta.

Luego, el controlador vuelve a llamar a la transacción para generar la salida (ya sea que la salida sea una solicitud o una respuesta). La llamada fluye de la transacción a la sesión, que utiliza el códec para convertir la semántica de nivel superior de la llamada en particular en los bytes apropiados para enviar por cable.

Se utilizan las mismas interfaces de controlador y transacción para crear solicitudes y manejar respuestas. La API es lo suficientemente genérica para permitir ambos. HTTPSessionse especializa de forma ligeramente diferente dependiendo de si está utilizando la conexión para emitir o responder a solicitudes HTTP.

Arquitectura central de proxígeno

Pasar a niveles más altos de abstracción, proxygen/HTTP servertiene un conjunto más simple de API y es la forma recomendada de interactuar proxygen cuando actúa como un servidor si no necesita el control total de las abstracciones de nivel inferior.

Los componentes básicos aquí son HTTPServer, RequestHandlerFactoryy RequestHandler. An HTTPServertoma alguna configuración y se le asigna un RequestHandlerFactory. Una vez que se inicia el servidor, el instalado RequestHandlerFactorygenera un RequestHandlerpara cada solicitud HTTP. RequestHandleres una interfaz simple que implementan los usuarios de la biblioteca. Las subclases de RequestHandlerdeben usar el miembro protegido heredado ResponseHandler* downstream_para enviar la respuesta.

usándolo
Proxygen es una biblioteca. Después de instalarlo, puede construir su servidor C++. Intentando cdacceder al directorio que contiene el servidor de eco en proxygen/httpserver/samples/echo/.

Después de compilar proxygen, puede iniciar el servidor de eco _build/proxygen/httpserver/proxygen_echo y verificar que funciona usando curl en una terminal diferente:

$ curl -v http://localhost:11000/
*   Trying 127.0.0.1...
* Connected to localhost (127.0.0.1) port 11000 (#0)
> GET / HTTP/1.1
> User-Agent: curl/7.35.0
> Host: localhost:11000
> Accept: */*
>
< HTTP/1.1 200 OK
< Request-Number: 1
< Date: Thu, 30 Oct 2014 17:07:36 GMT
< Connection: keep-alive
< Content-Length: 0
<
* Connection #0 to host localhost left intact
Puedes encontrar otras muestras:

un servidor simple que admite servidor push HTTP/2 ( _build/proxygen/httpserver/proxygen_push),
un servidor simple para archivos estáticos ( _build/proxygen/httpserver/proxygen_static)
un proxy de avance simple ( _build/proxygen/httpserver/proxygen_proxy)
un cliente tipo curl ( _build/proxygen/httpclient/samples/curl/proxygen_curl)
QUIC y HTTP/3
¡Proxygen es compatible con HTTP/3!

Depende de la biblioteca mvfst de Facebook para la implementación del transporte QUIC de IETF , por lo que hemos hecho que esa dependencia sea opcional. Puede compilar el código HTTP/3, las pruebas y los archivos binarios de muestra con ./build.sh --with-quic.

Esto también creará una práctica utilidad de línea de comandos que se puede usar como servidor y cliente HTTP/3.

Ejemplo de uso:

_build/proxygen/httpserver/hq --mode=server
_build/proxygen/httpserver/hq --mode=client --path=/
La utilidad admite el formato de registro qlog ; simplemente inicie el servidor con la --qlogger_pathopción y muchas perillas para ajustar tanto el transporte rápido como la capa http.

Documentación
Usamos Doxygen para la documentación interna de Proxygen. Puede generar una copia de estos documentos ejecutándolos doxygen Doxyfiledesde la raíz del proyecto. Querrás mirar html/namespaceproxygen.htmlpara empezar. Esto también generará follydocumentación.

Licencia
Ver LICENCIA .

contribuyendo
Las contribuciones a Proxygen son más que bienvenidas. Lea las pautas en CONTRIBUTING.md . Asegúrese de haber firmado el CLA antes de enviar una solicitud de extracción.

Sombrero blanco
Facebook tiene un programa de recompensas por la divulgación segura de errores de seguridad. Si encuentra una vulnerabilidad, realice el proceso descrito en esa página y no presente un problema público.

Lanzamientos 119
v2022.12.26.00
El último
4 days ago
