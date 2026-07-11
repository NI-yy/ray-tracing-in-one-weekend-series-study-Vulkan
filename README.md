# ray-tracing-in-one-weekend-study
A physically based path tracer implemented in C++ from scratch. Based on Peter Shirley's "Ray Tracing in One Weekend," featuring Lambertian, Metal, and Dielectric materials with defocus blur.

## CUDA study baseline

This repository is used as a CUDA study branch. It currently starts from an early CPU-only rendering state so that GPU acceleration can be introduced step by step.

Initial CPU timing result with a reduced debug render:

- Image size: 200 x 112
- Samples per pixel: 5
- Max depth: 10
- Total primary samples: 112,000
- Render time: 5.88546 seconds
- Pixels/sec: 3,805.99
- Primary samples/sec: 19,029.9
