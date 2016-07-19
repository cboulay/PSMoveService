// Derived From example 11-1 of "Learning OpenCV: Computer Vision with the OpenCV Library" by Gary Bradski

//-- includes -----
#include "AppStage_DistortionCalibration.h"
#include "AppStage_TrackerSettings.h"
#include "AppStage_MainMenu.h"
#include "AssetManager.h"
#include "App.h"
#include "Camera.h"
#include "ClientLog.h"
#include "MathUtility.h"
#include "Renderer.h"
#include "UIConstants.h"
#include "PSMoveProtocolInterface.h"
#include "PSMoveProtocol.pb.h"
#include "SharedTrackerState.h"

#include "SDL_keycode.h"
#include "SDL_opengl.h"

#include <imgui.h>

#include "opencv2/opencv.hpp"
#include "opencv2/calib3d/calib3d.hpp"

#include <vector>

#ifdef _MSC_VER
#pragma warning (disable: 4996) // 'This function or variable may be unsafe': snprintf
#define snprintf _snprintf
#endif

//-- statics ----
const char *AppStage_DistortionCalibration::APP_STAGE_NAME = "DistortionCalibration";

//-- constants -----
static const char *k_video_display_mode_names[] = {
    "BGR",
    "Grayscale",
    "Undistorted"
};

#define PATTERN_W 9 // Internal corners
#define PATTERN_H 6
#define CORNER_COUNT (PATTERN_W*PATTERN_H)
#define DESIRED_CAPTURE_BOARD_COUNT 100

#define BOARD_MOVED_PIXEL_DIST 5 
#define BOARD_MOVED_SQUARED_ERROR_SUM (BOARD_MOVED_PIXEL_DIST*BOARD_MOVED_PIXEL_DIST)*CORNER_COUNT

//-- private definitions -----
class OpenCVBufferState
{
public:
    OpenCVBufferState(int width, int height)
        : frameWidth(width)
        , frameHeight(height)
        , capturedBoardCount(0)
    {
        // Video Frame data
        bgrSourceBuffer = new cv::Mat(height, width, CV_8UC3);
        gsBuffer = new cv::Mat(height, width, CV_8UC1);
        gsBGRBuffer = new cv::Mat(height, width, CV_8UC3);
        bgrUndistortBuffer = new cv::Mat(height, width, CV_8UC3);

        // Chessboard state
        image_points = new cv::Mat(DESIRED_CAPTURE_BOARD_COUNT * CORNER_COUNT, 2, CV_32FC1);
        object_points = new cv::Mat(DESIRED_CAPTURE_BOARD_COUNT * CORNER_COUNT, 3, CV_32FC1);
        intrinsic_matrix = new cv::Mat(3, 3, CV_32FC1);
        distortion_coeffs = new cv::Mat(5, 1, CV_32FC1);

        // Distortion state
        distortionMapX = new cv::Mat(cv::Size(frameWidth, frameHeight), CV_32FC1);
        distortionMapY = new cv::Mat(cv::Size(frameWidth, frameHeight), CV_32FC1);

        resetCaptureState();
        resetCalibrationState();
    }

    virtual ~OpenCVBufferState()
    {
        // Video Frame data
        delete bgrSourceBuffer;
        delete gsBuffer;
        delete gsBGRBuffer;
        delete bgrUndistortBuffer;

        // Chessboard state
        delete image_points;
        delete object_points;
        delete intrinsic_matrix;
        delete distortion_coeffs;

        // Distortion state
        delete distortionMapX;
        delete distortionMapY;
    }

    void resetCaptureState()
    {
        capturedBoardCount= 0;
        corners.clear();
        *image_points= cv::Mat::zeros(DESIRED_CAPTURE_BOARD_COUNT * CORNER_COUNT, 2, CV_32FC1);
        *object_points= cv::Mat::zeros(DESIRED_CAPTURE_BOARD_COUNT * CORNER_COUNT, 3, CV_32FC1);
    }

    void resetCalibrationState()
    {
        reprojectionError= 0.f;

        // Initialize the intrinsic matrix such that the two focal lengths have a ratio of 1.0
        *intrinsic_matrix = cv::Mat::zeros(3, 3, CV_32FC1);
        intrinsic_matrix->at<float>(0, 0)= 1.0f;
        intrinsic_matrix->at<float>(1, 1)= 1.0f;

        // Clear out the distortion co-efficients
        *distortion_coeffs = cv::Mat::zeros(5, 1, CV_32FC1);
    }

    void applyVideoFrame(const unsigned char *video_buffer)
    {
        const cv::Mat videoBufferMat(frameHeight, frameWidth, CV_8UC3, const_cast<unsigned char *>(video_buffer));

        // Copy and Flip image about the x-axis
        videoBufferMat.copyTo(*bgrSourceBuffer);

        // Convert the video buffer to a grayscale image
        cv::cvtColor(*bgrSourceBuffer, *gsBuffer, cv::COLOR_BGR2GRAY);
        cv::cvtColor(*gsBuffer, *gsBGRBuffer, cv::COLOR_GRAY2BGR);
    }

    void findAndAppendNewChessBoard()
    {
        if (capturedBoardCount < DESIRED_CAPTURE_BOARD_COUNT)
        {
            std::vector<cv::Point2f> new_corners;

            // Find chessboard corners:
            if (cv::findChessboardCorners(
                    *gsBuffer, 
                    cv::Size(PATTERN_W, PATTERN_H), 
                    new_corners, // output corners
                    cv::CALIB_CB_ADAPTIVE_THRESH 
                    + cv::CALIB_CB_FILTER_QUADS 
                    // + cv::CALIB_CB_NORMALIZE_IMAGE is suuuper slow
                    + cv::CALIB_CB_FAST_CHECK))
            {
                // Get subpixel accuracy on those corners
                cv::cornerSubPix(
                    *gsBuffer, 
                    new_corners, // corners to refine
                    cv::Size(11, 11), // winSize- Half of the side length of the search window
                    cv::Size(-1, -1), // zeroZone- (-1,-1) means no dead zone in search
                    cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.1));

                // Append the new chessboard corner pixels into the image_points matrix
                // Append the corresponding 3d chessboard corners into the object_points matrix
                if (new_corners.size() == CORNER_COUNT) 
                {
                    bool bCornersChanged= false;

                    // See if the board moved enough to be considered a new location
                    if (corners.size() > 0)
                    {
                        float squared_error_sum= 0.f;

                        for (int corner_index= 0; corner_index < CORNER_COUNT; ++corner_index)
                        {
                            float squared_error= static_cast<float>(cv::norm(new_corners[corner_index] - corners[corner_index]));

                            squared_error_sum+= squared_error;
                        }

                        bCornersChanged= squared_error_sum >= BOARD_MOVED_SQUARED_ERROR_SUM;
                    }
                    else
                    {
                        bCornersChanged= true;
                    }

                    // If it's a valid new location, append it to the board list
                    if (bCornersChanged)
                    {
                        int write_offset = capturedBoardCount * CORNER_COUNT;

                        for (int write_index = write_offset, corner_index = 0; 
                            corner_index < CORNER_COUNT; 
                            ++write_index, ++corner_index) 
                        {
                            image_points->at<float>(write_index, 0)= new_corners[corner_index].x;
                            image_points->at<float>(write_index, 1)= new_corners[corner_index].y;

                            object_points->at<float>(write_index, 0)= static_cast<float>(corner_index) / static_cast<float>(PATTERN_W);
                            object_points->at<float>(write_index, 1)= static_cast<float>(corner_index % PATTERN_W);
                            object_points->at<float>(write_index, 2)= 0.f;
                        }

                        capturedBoardCount++;

                        // Keep track of the path of the corners of the chessboard
                        upperCornerPath.push_back(new_corners[0]);
                        lowerCornerPath.push_back(new_corners[CORNER_COUNT-1]);

                        // Remember the last set of valid corners
                        corners= new_corners;
                    }
                }
            }
        }
    }

    bool computeCameraCalibration()
    {
        bool bSuccess= false;

        if (capturedBoardCount >= DESIRED_CAPTURE_BOARD_COUNT)
        {
            // Compute the camera intrinsic matrix and distortion parameters
            reprojectionError= 
                cv::calibrateCamera(
                    *object_points, *image_points,
                    cv::Size(frameWidth, frameHeight), 
                    *intrinsic_matrix, *distortion_coeffs, // Output we care about
                    cv::noArray(), cv::noArray(), // best fit board poses as rvec/tvec pairs
                    cv::CALIB_FIX_ASPECT_RATIO,
                    cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, DBL_EPSILON));

            cv::initUndistortRectifyMap(
                *intrinsic_matrix, *distortion_coeffs, 
                cv::noArray(), // unneeded rectification transformation computed by stereoRectify()
                                   // newCameraMatrix - can be computed by getOptimalNewCameraMatrix(), but
                *intrinsic_matrix, // "In case of a monocular camera, newCameraMatrix is usually equal to cameraMatrix"
                cv::Size(frameWidth, frameHeight),
                CV_32FC1, // Distortion map type
                *distortionMapX, *distortionMapY);

            bSuccess= true;
        }

        return bSuccess;
    }

    int frameWidth;
    int frameHeight;

    // Video frame buffers
    cv::Mat *bgrSourceBuffer;
    cv::Mat *gsBuffer;
    cv::Mat *gsBGRBuffer;
    cv::Mat *bgrUndistortBuffer;

    // Chess board computed state
    int capturedBoardCount;
    std::vector<cv::Point2f> corners;
    std::vector<cv::Point2f> upperCornerPath;
    std::vector<cv::Point2f> lowerCornerPath;
    cv::Mat *image_points;
    cv::Mat *object_points;

    // Calibration state
    double reprojectionError;
    cv::Mat *intrinsic_matrix;
    cv::Mat *distortion_coeffs;

    // Distortion preview
    cv::Mat *distortionMapX;
    cv::Mat *distortionMapY;
};

//-- public methods -----
AppStage_DistortionCalibration::AppStage_DistortionCalibration(App *app)
    : AppStage(app)
    , m_menuState(AppStage_DistortionCalibration::inactive)
    , m_bStreamIsActive(false)
    , m_tracker_view(nullptr)
    , m_video_texture(nullptr)
    , m_opencv_state(nullptr)
    , m_videoDisplayMode(AppStage_DistortionCalibration::eVideoDisplayMode::mode_bgr)
{ }

void AppStage_DistortionCalibration::enter()
{
    const AppStage_TrackerSettings *trackerSettings =
        m_app->getAppStage<AppStage_TrackerSettings>();
    const ClientTrackerInfo *trackerInfo = trackerSettings->getSelectedTrackerInfo();
    assert(trackerInfo->tracker_id != -1);

    m_app->setCameraType(_cameraFixed);

    assert(m_tracker_view == nullptr);
    m_tracker_view= ClientPSMoveAPI::allocate_tracker_view(*trackerInfo);

    // Crank up the exposure and gain so that we can see the chessboard
    request_tracker_set_temp_exposure(255.f);
    request_tracker_set_temp_gain(128.f);

    assert(!m_bStreamIsActive);
    request_tracker_start_stream();
}

void AppStage_DistortionCalibration::exit()
{
    m_menuState = AppStage_DistortionCalibration::inactive;

    if (m_opencv_state != nullptr)
    {
        delete m_opencv_state;
        m_opencv_state= nullptr;
    }

    // Revert unsaved modifications to the tracker settings
    request_tracker_reload_settings();

    ClientPSMoveAPI::free_tracker_view(m_tracker_view);
    m_tracker_view = nullptr;
}

void AppStage_DistortionCalibration::update()
{
    if (m_menuState == AppStage_DistortionCalibration::capture ||
        m_menuState == AppStage_DistortionCalibration::complete)
    {
        assert(m_video_texture != nullptr);

        // Try and read the next video frame from shared memory
        if (m_tracker_view->pollVideoStream())
        {
            const unsigned char *video_frame_buffer= m_tracker_view->getVideoFrameBuffer();

            // Update the video frame buffers
            m_opencv_state->applyVideoFrame(video_frame_buffer);

            // Update the video frame display texture
            switch (m_videoDisplayMode)
            {
            case AppStage_DistortionCalibration::mode_bgr:
                m_video_texture->copyBufferIntoTexture(m_opencv_state->bgrSourceBuffer->data);
                break;
            case AppStage_DistortionCalibration::mode_grayscale:
                m_video_texture->copyBufferIntoTexture(m_opencv_state->gsBGRBuffer->data);
                break;
            case AppStage_DistortionCalibration::mode_undistored:
                m_video_texture->copyBufferIntoTexture(m_opencv_state->bgrUndistortBuffer->data);
                break;
            default:
                assert(0 && "unreachable");
                break;
            }

            if (m_menuState == AppStage_DistortionCalibration::capture)
            {
                // Update the chess board capture state
                m_opencv_state->findAndAppendNewChessBoard();
            }
        }
    }
}

void AppStage_DistortionCalibration::render()
{
    if (m_menuState == AppStage_DistortionCalibration::capture ||
        m_menuState == AppStage_DistortionCalibration::complete)
    {
        assert(m_video_texture != nullptr);
        unsigned int texture_id = m_video_texture->texture_id;

        if (texture_id != 0)
        {
            drawFullscreenTexture(texture_id);
        }

        if (m_menuState == AppStage_DistortionCalibration::capture)
        {
            // Draw the most recently capture chessboard
            if (m_opencv_state->corners.size() > 0)
            {
                drawOpenCVChessBoard(
                    static_cast<float>(m_opencv_state->frameWidth), 
                    static_cast<float>(m_opencv_state->frameHeight), 
                    reinterpret_cast<float *>(m_opencv_state->corners.data()), // cv::point2f is just two floats 
                    static_cast<int>(m_opencv_state->corners.size()));
            }
        }
    }
}

void AppStage_DistortionCalibration::renderUI()
{
    const float k_panel_width = 300.f;
    const char *k_window_title = "Tracker Test";
    const ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_ShowBorders |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse;

    switch (m_menuState)
    {
    case eTrackerMenuState::capture:
        {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2.f - k_panel_width / 2.f, 20.f));
            ImGui::SetNextWindowSize(ImVec2(k_panel_width, 200));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            if (m_opencv_state != nullptr)
            {
                if (ImGui::Button("<##Filter"))
                {
                    m_videoDisplayMode =
                        static_cast<eVideoDisplayMode>(
                        (m_videoDisplayMode + eVideoDisplayMode::MAX_VIDEO_DISPLAY_MODES - 1)
                        % eVideoDisplayMode::MAX_VIDEO_DISPLAY_MODES);
                }
                ImGui::SameLine();
                if (ImGui::Button(">##Filter"))
                {
                    m_videoDisplayMode =
                        static_cast<eVideoDisplayMode>(
                        (m_videoDisplayMode + 1) % eVideoDisplayMode::MAX_VIDEO_DISPLAY_MODES);
                }
                ImGui::SameLine();
                ImGui::Text("Video Filter Mode: %s", k_video_display_mode_names[m_videoDisplayMode]);
            }

            if (ImGui::Button("Cancel"))
            {
                request_exit();
            }

            ImGui::End();
        } break;

    case eTrackerMenuState::complete:
        {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2.f - k_panel_width / 2.f, 20.f));
            ImGui::SetNextWindowSize(ImVec2(k_panel_width, 200));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            ImGui::Text("Calibration complete!");

            if (ImGui::Button("Ok"))
            {
                request_exit();
            }

            if (ImGui::Button("Redo Calibration"))
            {
                m_opencv_state->resetCaptureState();
                m_opencv_state->resetCalibrationState();
                m_menuState= eTrackerMenuState::capture;
            }

            ImGui::End();
        } break;

    case eTrackerMenuState::pendingTrackerStartStreamRequest:
        {
            ImGui::SetNextWindowPosCenter();
            ImGui::SetNextWindowSize(ImVec2(k_panel_width, 50));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            ImGui::Text("Waiting for tracker stream to start...");

            ImGui::End();
        } break;

    case eTrackerMenuState::failedTrackerStartStreamRequest:
    case eTrackerMenuState::failedTrackerOpenStreamRequest:
        {
            ImGui::SetNextWindowPosCenter();
            ImGui::SetNextWindowSize(ImVec2(k_panel_width, 130));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            if (m_menuState == eTrackerMenuState::failedTrackerStartStreamRequest)
                ImGui::Text("Failed to start tracker stream!");
            else
                ImGui::Text("Failed to open tracker stream!");

            if (ImGui::Button("Ok"))
            {
                m_app->setAppStage(AppStage_TrackerSettings::APP_STAGE_NAME);
            }

            if (ImGui::Button("Return to Main Menu"))
            {
                m_app->setAppStage(AppStage_MainMenu::APP_STAGE_NAME);
            }

            ImGui::End();
        } break;

    case eTrackerMenuState::pendingTrackerStopStreamRequest:
        {
            ImGui::SetNextWindowPosCenter();
            ImGui::SetNextWindowSize(ImVec2(k_panel_width, 50));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            ImGui::Text("Waiting for tracker stream to stop...");

            ImGui::End();
        } break;

    case eTrackerMenuState::failedTrackerStopStreamRequest:
        {
            ImGui::SetNextWindowPosCenter();
            ImGui::SetNextWindowSize(ImVec2(k_panel_width, 130));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            ImGui::Text("Failed to stop tracker stream!");

            if (ImGui::Button("Ok"))
            {
                m_app->setAppStage(AppStage_TrackerSettings::APP_STAGE_NAME);
            }

            if (ImGui::Button("Return to Main Menu"))
            {
                m_app->setAppStage(AppStage_MainMenu::APP_STAGE_NAME);
            }

            ImGui::End();
        } break;

    default:
        assert(0 && "unreachable");
    }
}

void AppStage_DistortionCalibration::request_tracker_start_stream()
{
    if (m_menuState != AppStage_DistortionCalibration::pendingTrackerStartStreamRequest)
    {
        m_menuState = AppStage_DistortionCalibration::pendingTrackerStartStreamRequest;

        // Tell the psmove service that we want to start streaming data from the tracker
        ClientPSMoveAPI::register_callback(
            ClientPSMoveAPI::start_tracker_data_stream(m_tracker_view),
            AppStage_DistortionCalibration::handle_tracker_start_stream_response, this);
    }
}

void AppStage_DistortionCalibration::handle_tracker_start_stream_response(
    const ClientPSMoveAPI::ResponseMessage *response,
    void *userdata)
{
    AppStage_DistortionCalibration *thisPtr = static_cast<AppStage_DistortionCalibration *>(userdata);

    switch (response->result_code)
    {
    case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            ClientTrackerView *trackerView= thisPtr->m_tracker_view;

            thisPtr->m_bStreamIsActive = true;

            // Open the shared memory that the vidoe stream is being written to
            if (trackerView->openVideoStream())
            {
                int width= trackerView->getVideoFrameWidth();
                int height= trackerView->getVideoFrameHeight();

                // Create a texture to render the video frame to
                thisPtr->m_video_texture = new TextureAsset();
                thisPtr->m_video_texture->init(
                    width,
                    height,
                    GL_RGB, // texture format
                    GL_BGR, // buffer format
                    nullptr);

                // Allocate an opencv buffer 
                thisPtr->m_opencv_state = new OpenCVBufferState(width, height);

                // Start capturing chess boards
                thisPtr->m_menuState = AppStage_DistortionCalibration::capture;
            }
            else
            {
                thisPtr->m_menuState = AppStage_DistortionCalibration::failedTrackerOpenStreamRequest;
            }
        } break;

    case ClientPSMoveAPI::_clientPSMoveResultCode_error:
    case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        {
            thisPtr->m_menuState = AppStage_DistortionCalibration::failedTrackerStartStreamRequest;
        } break;
    }
}

void AppStage_DistortionCalibration::request_tracker_stop_stream()
{
    if (m_bStreamIsActive && m_menuState != AppStage_DistortionCalibration::pendingTrackerStopStreamRequest)
    {
        m_menuState = AppStage_DistortionCalibration::pendingTrackerStopStreamRequest;

        // Tell the psmove service that we want to stop streaming data from the tracker        
        ClientPSMoveAPI::register_callback(
            ClientPSMoveAPI::stop_tracker_data_stream(m_tracker_view), 
            AppStage_DistortionCalibration::handle_tracker_stop_stream_response, this);
    }
}

void AppStage_DistortionCalibration::handle_tracker_stop_stream_response(
    const ClientPSMoveAPI::ResponseMessage *response,
    void *userdata)
{
    AppStage_DistortionCalibration *thisPtr = static_cast<AppStage_DistortionCalibration *>(userdata);

    // In either case consider the stream as now inactive
    thisPtr->m_bStreamIsActive = false;

    switch (response->result_code)
    {
    case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            thisPtr->m_menuState = AppStage_DistortionCalibration::inactive;

            // Close the shared memory buffer
            thisPtr->m_tracker_view->closeVideoStream();

            // Free the texture we were rendering to
            if (thisPtr->m_video_texture != nullptr)
            {
                delete thisPtr->m_video_texture;
                thisPtr->m_video_texture = nullptr;
            }

            // After closing the stream, we should go back to the tracker settings
            thisPtr->m_app->setAppStage(AppStage_TrackerSettings::APP_STAGE_NAME);
        } break;

    case ClientPSMoveAPI::_clientPSMoveResultCode_error:
    case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        {
            thisPtr->m_menuState = AppStage_DistortionCalibration::failedTrackerStopStreamRequest;
        } break;
    }
}

void AppStage_DistortionCalibration::request_tracker_set_temp_gain(float gain)
{
    // Tell the psmove service that we want to change gain, but not save the change
    RequestPtr request(new PSMoveProtocol::Request());
    request->set_type(PSMoveProtocol::Request_RequestType_SET_TRACKER_GAIN);
    request->mutable_request_set_tracker_gain()->set_tracker_id(m_tracker_view->getTrackerId());
    request->mutable_request_set_tracker_gain()->set_value(gain);
    request->mutable_request_set_tracker_gain()->set_save_setting(false);

    ClientPSMoveAPI::eat_response(ClientPSMoveAPI::send_opaque_request(&request));
}

void AppStage_DistortionCalibration::request_tracker_set_temp_exposure(float exposure)
{
    // Tell the psmove service that we want to change exposure, but not save the change.
    RequestPtr request(new PSMoveProtocol::Request());
    request->set_type(PSMoveProtocol::Request_RequestType_SET_TRACKER_EXPOSURE);
    request->mutable_request_set_tracker_exposure()->set_tracker_id(m_tracker_view->getTrackerId());
    request->mutable_request_set_tracker_exposure()->set_value(exposure);
    request->mutable_request_set_tracker_exposure()->set_save_setting(false);

    ClientPSMoveAPI::eat_response(ClientPSMoveAPI::send_opaque_request(&request));
}


void AppStage_DistortionCalibration::request_tracker_reload_settings()
{
    RequestPtr request(new PSMoveProtocol::Request());
    request->set_type(PSMoveProtocol::Request_RequestType_RELOAD_TRACKER_SETTINGS);
    request->mutable_request_reload_tracker_settings()->set_tracker_id(m_tracker_view->getTrackerId());

    ClientPSMoveAPI::eat_response(ClientPSMoveAPI::send_opaque_request(&request));
}

void AppStage_DistortionCalibration::request_exit()
{
    if (m_bStreamIsActive)
    {
        const AppStage_TrackerSettings *trackerSettings =
            m_app->getAppStage<AppStage_TrackerSettings>();
        const ClientTrackerInfo *trackerInfo = trackerSettings->getSelectedTrackerInfo();

        request_tracker_stop_stream();
    }
    else
    {
        m_app->setAppStage(AppStage_TrackerSettings::APP_STAGE_NAME);
    }
}