#ifndef CAMERA_H
#define CAMERA_H

#include <chrono>

#include "hittable.h"
#include "material.h"

class camera {
	public:
		double aspect_ratio = 1.0; // Ratio of image width over height
		int image_width = 100; // Rendered image width in pixel count
		int samples_per_pixel = 10; // Count of random samples for each pixel
		int max_depth = 10; // Maximum number of ray bounces into scene

		double vfov = 90; // Vertical view angle (field of view)
		point3 lookfrom = point3(0, 0, 0);
		point3 lookat = point3(0, 0, -1);
		vec3 vup = vec3(0, 1, 0);

		double defocus_angle = 0; // Variation angle of rays through each pixel
		double focus_dist = 10; // Distance from camera lookfrom point to plane of perfect focus

		void render(const hittable& world) {
			initialize();

			const auto render_start = std::chrono::high_resolution_clock::now();
			const auto total_pixels = static_cast<long long>(image_width) * image_height;
			const auto total_samples = total_pixels * samples_per_pixel;

			std::clog
				<< "Render settings:\n"
				<< "  image: " << image_width << "x" << image_height << '\n'
				<< "  samples_per_pixel: " << samples_per_pixel << '\n'
				<< "  max_depth: " << max_depth << '\n'
				<< "  total primary samples: " << total_samples << "\n\n";

			
			std::ofstream image_file("image.ppm");
			image_file << "P3\n" << image_width << ' ' << image_height << "\n255\n";

			for (int j = 0; j < image_height; j++) {
				// represent progress
				std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush;

				for (int i = 0; i < image_width; i++) {
					color pixel_color(0, 0, 0);
					for (int sample = 0; sample < samples_per_pixel; sample++) {
						ray r = get_ray(i, j);
						pixel_color += ray_color(r, max_depth, world);
					}
					write_color(image_file, pixel_samples_scale * pixel_color);
				}
			}


			image_file.close();

			const auto render_end = std::chrono::high_resolution_clock::now();
			const std::chrono::duration<double> elapsed = render_end - render_start;
			const auto elapsed_seconds = elapsed.count();

			std::clog
				<< "\nDone.\n"
				<< "Render time: " << elapsed_seconds << " seconds\n"
				<< "Pixels/sec: " << (total_pixels / elapsed_seconds) << '\n'
				<< "Primary samples/sec: " << (total_samples / elapsed_seconds) << '\n';
		}

	private:
		int image_height;
		double pixel_samples_scale; // Color scale factor for a sum of pixel samples
		point3 center;
		point3 pixel00_loc;
		vec3 pixel_delta_u;
		vec3 pixel_delta_v;
		vec3 u, v, w;
		vec3 defocus_disk_u; // Defocus disk horizontal radius
		vec3 defocus_disk_v; // Defocus disk vertical radius


		void initialize() {
			image_height = int(image_width / aspect_ratio);
			image_height = (image_height < 1) ? 1 : image_height;

			pixel_samples_scale = 1.0 / samples_per_pixel;

			center = lookfrom;

			// Determine viewport dimensions.
			auto theta = degrees_to_radians(vfov);
			auto h = std::tan(theta / 2);
			auto viewport_height = 2 * h * focus_dist;
			auto viewport_width = viewport_height * (double(image_width) / image_height);

			// Calculate the u,v,w unit basis vectors for the camera coodinate frame.
			w = unit_vector(lookfrom - lookat);
			u = unit_vector(cross(vup, w));
			v = cross(w, u);

			// Calculate the vectors across the horizontal and down the vertical viewport edges.
			auto viewport_u = viewport_width * u;
			auto viewport_v = viewport_height * -v;

			// Calculate the horizontal and vertical delta vectors from pixel to pixel.
			pixel_delta_u = viewport_u / image_width;
			pixel_delta_v = viewport_v / image_height;

			auto viewport_upper_left = center - (focus_dist * w) - viewport_u / 2 - viewport_v / 2;
            pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

			// Calculate the camera defocus disk basis vectors.
			auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
			defocus_disk_u = u * defocus_radius;
			defocus_disk_v = v * defocus_radius;
		}

		ray get_ray(int i, int j) const {
			// Construct a camera ray originating from the defocus disk and directed at randomly samlpled
			// point around the pixel location i, j.

			auto offset = sample_square();
			auto pixel_sample = pixel00_loc + ((i + offset.x()) * pixel_delta_u) + ((j + offset.y()) * pixel_delta_v);

			auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();
			auto ray_direction = pixel_sample - ray_origin;

			return ray(ray_origin, ray_direction);
		}

		vec3 sample_square() const {
			// Returns the vector to a random point in the [-.5, -.5] - [+.5, +.5] unit square.
			return vec3(random_double() - 0.5, random_double() - 0.5, 0);
		}

		point3 defocus_disk_sample() const {
			// Returns a random point in the camera defocus disk.
			auto p = random_in_unit_disk();
			return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
		}


		color ray_color(const ray& r, int depth, const hittable& world) const {
			// If we've exceeded the ray bounce limit, no more light is gathered.
			if (depth <= 0)
				return color(0, 0, 0);

			hit_record rec;

			if (world.hit(r, interval(0.001, infinity), rec)) {
				//return 0.5 * (rec.normal + color(1, 1, 1)); // draw a sphere using normal vector value
				//vec3 direction = random_on_hemisphere(rec.normal); // more white when the surface facing sky.
				// vec3 direction = rec.normal + random_unit_vector(); // lambert reflection(?)
				ray scattered;
				color attenuation;
				if (rec.mat->scatter(r, rec, attenuation, scattered))
					return attenuation * ray_color(scattered, depth - 1, world);
				return color(0, 0, 0);
			}

			vec3 unit_direction = unit_vector(r.direction());
			auto a = 0.5 * (unit_direction.y() + 1.0);
			return (1.0 - a) * color(1.0, 1.0, 1.0) + a * color(0.5, 0.7, 1.0);
		}
};

#endif
